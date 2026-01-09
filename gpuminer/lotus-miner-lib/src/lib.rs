mod block;
pub mod miner;
pub mod settings;
mod sha256;
pub mod logger;

use eyre::Result;
pub use miner::Miner;
pub use settings::ConfigSettings;
pub use logger::{Log, LogSeverity, HashrateEntry, LoggerConfig, init_global_logger, LogEntry};

use std::{
    sync::{
        atomic::{AtomicU64, Ordering},
        Arc,
    },
    time::{Duration, SystemTime},
};

use block::{create_block, Block, GetRawUnsolvedBlockResponse};
use miner::{MiningSettings, Work};
use rand::{Rng, SeedableRng};
use reqwest::{RequestBuilder, StatusCode};
use serde::Deserialize;
use tokio::sync::{Mutex, MutexGuard};

pub struct Server {
    client: reqwest::Client,
    miner: std::sync::Mutex<Miner>,
    node_settings: Mutex<NodeSettings>,
    block_state: Mutex<BlockState>,
    // Use parking_lot style RwLock pattern - rng doesn't need async, use std::sync for speed
    rng: std::sync::Mutex<rand::rngs::StdRng>,
    metrics_timestamp: std::sync::Mutex<SystemTime>,
    metrics_nonces: AtomicU64,
    hashrate_data_points: Mutex<Vec<(SystemTime, u64)>>,
    last_total_nonces: AtomicU64,
    log: Log,
    report_hashrate_interval: Duration,
}

pub struct NodeSettings {
    pub bitcoind_url: String,
    pub bitcoind_user: String,
    pub bitcoind_password: String,
    pub rpc_poll_interval: u64,
    pub miner_addr: String,
    pub pool_mining: bool,
}

struct BlockState {
    current_work: Work,
    current_block: Option<Block>,
    next_block: Option<Block>,
    extra_nonce: u64,
}

pub type ServerRef = Arc<Server>;

impl Server {
    /// Create a new Server from configuration.
    /// 
    /// # Errors
    /// Returns an error if GPU initialization fails (e.g., no OpenCL devices found,
    /// invalid GPU index, or driver issues).
    pub fn from_config(config: ConfigSettings, report_hashrate_interval: Duration) -> Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        // Validate rpc_poll_interval - must be positive
        if config.rpc_poll_interval <= 0 {
            return Err(format!(
                "Invalid rpc_poll_interval: {}. Must be a positive integer.",
                config.rpc_poll_interval
            ).into());
        }
        
        // Validate kernel_size - reasonable bounds (8 to 30)
        if config.kernel_size < 8 || config.kernel_size > 30 {
            return Err(format!(
                "Invalid kernel_size: {}. Must be between 8 and 30.",
                config.kernel_size
            ).into());
        }
        
        // Optimized mining settings:
        // - local_work_size: 256 is a good default for most GPUs (power of 2, good occupancy)
        // - inner_iter_size: 32 provides good balance between kernel launches and work per thread
        // - kernel_size: Configurable via config, represents total work items per kernel dispatch
        let mining_settings = MiningSettings {
            local_work_size: 256,
            inner_iter_size: 32, // Increased from 16 for better GPU utilization
            kernel_size: 1 << config.kernel_size,
            sleep: 0,
            gpu_indices: vec![config.gpu_index as usize],
            kernel_type: config.kernel_type,
        };
        
        // Initialize the miner - this can fail if GPU is not available
        let miner = Miner::setup(mining_settings.clone())
            .map_err(|e| format!("Failed to initialize GPU miner: {}. Check your GPU drivers and OpenCL installation.", e))?;
        
        Ok(Server {
            miner: std::sync::Mutex::new(miner),
            client: reqwest::Client::new(),
            node_settings: Mutex::new(NodeSettings {
                bitcoind_url: config.rpc_url.clone(),
                bitcoind_user: config.rpc_user.clone(),
                bitcoind_password: config.rpc_password.clone(),
                rpc_poll_interval: config.rpc_poll_interval as u64,
                miner_addr: config.mine_to_address.clone(),
                pool_mining: config.pool_mining,
            }),
            block_state: Mutex::new(BlockState {
                current_work: Work::default(),
                current_block: None,
                next_block: None,
                extra_nonce: 0,
            }),
            rng: std::sync::Mutex::new(rand::rngs::StdRng::from_entropy()),
            metrics_timestamp: std::sync::Mutex::new(SystemTime::now()),
            metrics_nonces: AtomicU64::new(0),
            hashrate_data_points: Mutex::new(Vec::new()),
            last_total_nonces: AtomicU64::new(0),
            log: Log::new(),
            report_hashrate_interval,
        })
    }

    pub async fn run(self: ServerRef) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
        let t1 = tokio::spawn({
            let server = Arc::clone(&self);
            async move {
                let log = server.log();
                loop {
                    if let Err(err) = update_next_block(&server).await {
                        log.error(format!("update_next_block error: {:?}", err), Some("Miner"));
                    }
                    let rpc_poll_interval = server.node_settings.lock().await.rpc_poll_interval;
                    tokio::time::sleep(Duration::from_secs(rpc_poll_interval)).await;
                }
            }
        });
        let t2 = tokio::spawn({
            let server = Arc::clone(&self);
            async move {
                let log = server.log();
                loop {
                    if let Err(err) = mine_some_nonces(Arc::clone(&server)).await {
                        log.error(format!("mine_some_nonces error: {:?}", err), Some("Miner"));
                    }
                    // Yield to allow other tasks to run, but don't add unnecessary delay
                    // The mining operation itself provides natural pacing
                    tokio::task::yield_now().await;
                }
            }
        });
        t1.await?;
        t2.await?;
        Ok(())
    }

    pub async fn node_settings<'a>(&'a self) -> MutexGuard<'a, NodeSettings> {
        self.node_settings.lock().await
    }

    pub fn miner<'a>(&'a self) -> std::sync::MutexGuard<'a, Miner> {
        // Note: If the mutex is poisoned (a thread panicked while holding it),
        // we recover by taking ownership of the lock anyway. The miner state may
        // be inconsistent, but continuing is better than crashing the entire app.
        match self.miner.lock() {
            Ok(guard) => guard,
            Err(poisoned) => {
                log::warn!("Miner mutex was poisoned, recovering...");
                poisoned.into_inner()
            }
        }
    }

    pub fn log(&self) -> &Log {
        &self.log
    }

    async fn calculate_moving_average_hashrate(&self) -> f64 {
        let now = SystemTime::now();
        let current_total_nonces = self.metrics_nonces.load(Ordering::Acquire);
        let previous_total = self.last_total_nonces.swap(current_total_nonces, Ordering::AcqRel);
        let new_nonces = current_total_nonces.saturating_sub(previous_total);
        
        let mut data_points = self.hashrate_data_points.lock().await;
        data_points.push((now, new_nonces));
        
        // Retain data points from the last 60 seconds
        // Compare directly against `now` rather than using UNIX_EPOCH arithmetic
        let cutoff_duration = Duration::from_secs(60);
        data_points.retain(|(time, _)| {
            now.duration_since(*time).unwrap_or(Duration::MAX) <= cutoff_duration
        });
        
        // Calculate total nonces and time span across all retained data points
        let mut total_nonces = 0u64;
        let oldest_timestamp = data_points.first().map(|(time, _)| *time).unwrap_or(now);
        
        for (_, nonces) in data_points.iter() {
            total_nonces = total_nonces.saturating_add(*nonces);
        }
        
        // Calculate time span in seconds
        let time_span = now.duration_since(oldest_timestamp)
            .unwrap_or_default()
            .as_secs_f64()
            .max(0.1); // Avoid division by zero by ensuring at least 0.1 seconds
        
        // Calculate initial hashrate (nonces per second)
        let raw_hashrate = total_nonces as f64 / time_span;
        
        // Apply warm-up period stabilization logic
        let stabilized_hashrate = if time_span < 15.0 {
            // During warm-up period (less than 15 seconds of data)
            
            // Use a sliding scale that starts at a conservative estimate and gradually 
            // approaches the raw value as we get more data
            let warm_up_factor = (time_span / 15.0).min(1.0);
            
            // Use the most recent point's rate as a baseline, but cap it at a reasonable value
            // This prevents extremely high initial readings
            let single_point_rate = if data_points.len() > 1 {
                let (time1, _) = data_points[data_points.len() - 1];
                let (time2, nonces2) = data_points[data_points.len() - 2];
                
                let point_time_diff = time1.duration_since(time2)
                    .unwrap_or_default()
                    .as_secs_f64()
                    .max(0.1);
                
                (nonces2 as f64) / point_time_diff
            } else {
                raw_hashrate
            };
            
            // Cap the initial estimate to prevent unrealistically high values
            let capped_rate = single_point_rate.min(3_000_000_000.0); // Cap at 3 GH/s initially
            
            // Gradually blend between the capped initial estimate and the raw calculation
            let result = capped_rate * (1.0 - warm_up_factor) + raw_hashrate * warm_up_factor;
            
            // Log that we're stabilizing the hashrate during warm-up
            self.log.debug(
                format!(
                    "Stabilizing hashrate during {:.1}s warm-up period: raw {:.2} GH/s → stabilized {:.2} GH/s ({}% warm-up)",
                    time_span,
                    raw_hashrate / 1_000_000_000.0,
                    result / 1_000_000_000.0,
                    (warm_up_factor * 100.0) as u32
                ),
                Some("Hashrate")
            );
            
            result
        } else {
            // We have enough data, use the raw calculated value
            raw_hashrate
        };
        
        stabilized_hashrate
    }
}

async fn init_request(server: &Server) -> RequestBuilder {
    let node_settings = server.node_settings.lock().await;
    server.client.post(&node_settings.bitcoind_url).basic_auth(
        &node_settings.bitcoind_user,
        Some(&node_settings.bitcoind_password),
    )
}

fn display_hash(hash: &[u8]) -> String {
    let mut hash = hash.to_vec();
    hash.reverse();
    hex::encode(&hash)
}

async fn update_next_block(server: &Server) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let log = server.log();
    let url = server.node_settings.lock().await.bitcoind_url.clone();
    
    let request_start = std::time::Instant::now();
    log.debug(format!("🛰️ [DEBUG] RPC call: getrawunsolvedblock to URL: {}", url), Some("RPC"));
    
    let request_body = {
        let miner_addr = server.node_settings.lock().await.miner_addr.clone();
        format!(
            r#"{{"method":"getrawunsolvedblock","params":["{}"]}}"#,
            miner_addr
        )
    };
    
    let response = init_request(&server)
        .await
        .body(request_body)
        .send()
        .await?;
        
    let status = response.status();
    let network_time = request_start.elapsed();
    log.debug(format!("🛰️ [DEBUG] RPC response status: {} for getrawunsolvedblock (took: {}ms)", 
        status, network_time.as_millis()), Some("RPC"));
    
    let response_str = response.text().await?;
    log.debug(format!("🛰️ [DEBUG] RPC response body length: {} characters", response_str.len()), Some("RPC"));
    
    let response: Result<GetRawUnsolvedBlockResponse, _> = serde_json::from_str(&response_str);
    log.debug("🛰️ [DEBUG] RPC response parsed for getrawunsolvedblock", Some("RPC"));
    
    let response = match response {
        Ok(response) => response,
        Err(_) => {
            log.error(format!(
                "getrawunsolvedblock failed ({}): {}",
                status, response_str
            ), Some("Miner"));
            if status == StatusCode::UNAUTHORIZED {
                log.error("It seems you specified the wrong username/password", Some("Miner"));
            }
            return Ok(());
        }
    };
    
    let unsolved_block = match response.result {
        Some(unsolved_block) => unsolved_block,
        None => {
            log.error(format!(
                "getrawunsolvedblock failed: {}",
                response.error.unwrap_or("unknown error".to_string())
            ), Some("Miner"));
            return Ok(());
        }
    };
    
    let block = match create_block(&unsolved_block) {
        Ok(block) => block,
        Err(e) => {
            log.error(format!(
                "Failed to parse block from node response: {}. This may indicate a protocol mismatch.",
                e
            ), Some("Miner"));
            return Ok(());
        }
    };
    
    let total_time = request_start.elapsed();
    
    let mut block_state = server.block_state.lock().await;
    
    if let Some(current_block) = &block_state.current_block {
        if current_block.prev_hash() != block.prev_hash() {
            log.info(format!(
                "🔀 Switched to new chain tip: {}",
                display_hash(&block.prev_hash())
            ), Some("Miner"));
        }
    } else {
        log.info(format!(
            "🌱 Started mining on chain tip: {}",
            display_hash(&block.prev_hash())
        ), Some("Miner"));
    }
    
    block_state.extra_nonce += 1;
    block_state.next_block = Some(block);
    
    log.debug(format!("🛰️ Work fetch completed in {}ms (network: {}ms)", 
        total_time.as_millis(), network_time.as_millis()), Some("RPC"));
        
    Ok(())
}

async fn submit_block(server: &Server, block: &Block) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    #[derive(Deserialize)]
    struct SubmitBlockResponse {
        result: Option<String>,
        error: Option<serde_json::Value>,
    }
    let log = server.log();
    let mut serialized_block = block.header.to_vec();
    serialized_block.extend_from_slice(&block.body);
    
    let (url, user, password, pool_mining, miner_addr) = {
        let node_settings = server.node_settings.lock().await;
        (
            node_settings.bitcoind_url.clone(),
            node_settings.bitcoind_user.clone(), 
            node_settings.bitcoind_password.clone(),
            node_settings.pool_mining,
            node_settings.miner_addr.clone()
        )
    };
    log.info(format!("🛰️ Submitting share to pool: {}", url), Some("RPC"));
    let block_hex = hex::encode(&serialized_block);
    
    let request_body = if !pool_mining {
        format!(
            r#"{{"jsonrpc":"2.0","id":"miner","method":"submitblock","params":["{}"]}}"#,
            block_hex
        )
    } else {
        format!(
            r#"{{"jsonrpc":"2.0","id":"miner","method":"submitblock","params":["{}", "{}"]}}"#,
            block_hex,
            miner_addr
        )
    };
    
    log.debug(format!("🛰️ [DEBUG] RPC call: submitblock to URL: {}", url), Some("RPC"));
    
    let response = server.client.post(&url)
        .basic_auth(user, Some(password))
        .header("Content-Type", "application/json")
        .body(request_body)
        .send()
        .await?;
    log.debug(format!("🛰️ [DEBUG] RPC response status: {} for submitblock", response.status()), Some("RPC"));
    let status = response.status();
    let response_text = response.text().await?;
    
    log.debug(format!("🛰️ [DEBUG] RPC response body length: {} characters for submitblock", response_text.len()), Some("RPC"));
    
    let response: Result<SubmitBlockResponse, _> = serde_json::from_str(&response_text);
    
    match response {
        Ok(parsed) => {
            match parsed.result {
                None => {
                    if let Some(error) = parsed.error {
                        log.error(format!("REJECTED BLOCK: Error {:?}", error), Some("Share"));
                        log.error("Something is misconfigured; make sure you run the latest lotusd/Lotus-QT and lotus-gpu-miner.", Some("Share"));
                    } else {
                        if pool_mining {
                            log.info(
                                format!(
                                    "🎉 Share accepted by \"{}\" for \"{}\" !",
                                    url, miner_addr
                                ),
                                Some("Share")
                            );
                        } else {
                            log.info("🎉 Block accepted!", Some("Share"));
                        }
                    }
                },
                Some(reason) => {
                    if reason.is_empty() {
                        if pool_mining {
                            log.info(
                                format!(
                                    "🎉 Share accepted by \"{}\" for \"{}\" !",
                                    url, miner_addr
                                ),
                                Some("Share")
                            );
                        } else {
                            log.info("🎉 Block accepted!", Some("Share"));
                        }
                    } else {
                        if pool_mining {
                            log.error(format!("REJECTED SHARE: {}", reason), Some("Share"));
                        } else {
                            log.error(format!("REJECTED BLOCK: {}", reason), Some("Share"));
                        }
                        if reason == "inconclusive" {
                            log.warn(
                                "This is an orphan race; might be fixed by lowering rpc_poll_interval or \
                                updating to the newest lotus-gpu-miner.", Some("Share")
                            );
                        } else {
                            log.error(
                                "Something is misconfigured; make sure you run the latest \
                                lotusd/Lotus-QT and lotus-gpu-miner.", Some("Share")
                            );
                        }
                    }
                }
            }
        },
        Err(e) => {
            log.error(format!("Failed to parse response: {} (Status: {})\nResponse: {}", 
                e, status, response_text), Some("Miner"));
        }
    }
    
    log.info("✅ Share submission completed, mining continues uninterrupted", Some("Miner"));
    
    Ok(())
}

async fn mine_some_nonces(server: ServerRef) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let log = server.log();
    let pool_mining = server.node_settings.lock().await.pool_mining;
    
    log.info(format!("🚀 Starting mining loop (pool mode: {})", if pool_mining { "enabled" } else { "disabled" }), Some("Miner"));
    
    {
        log.info("🧠 Initializing work queue for continuous mining...", Some("Miner"));
        let block_state = server.block_state.lock().await;
        if block_state.next_block.is_none() && block_state.current_block.is_none() {
            drop(block_state);
            if let Err(err) = update_next_block(&server).await {
                log.error(format!("Failed to initialize first block: {:?}", err), Some("Miner"));
                tokio::time::sleep(Duration::from_millis(100)).await;
            }
        }
    }
    
    if pool_mining {
        let server_clone = Arc::clone(&server);
        tokio::spawn(async move {
            let log = server_clone.log();
            log.info("🚀 Starting high-efficiency mining processor", Some("Miner"));
            
            // Start the work prefetcher in a separate task
            tokio::spawn({
                let inner_server = Arc::clone(&server_clone);
                async move {
                    let log = inner_server.log();
                    log.debug("🔄 Starting zero-latency work prefetcher", Some("Miner"));
                    loop {
                        let needs_work = {
                            let block_state = inner_server.block_state.lock().await;
                            block_state.next_block.is_none()
                        };
                        
                        if needs_work {
                            if let Err(err) = update_next_block(&inner_server).await {
                                log.error(format!("Failed to prefetch next block: {:?}", err), Some("Miner"));
                                tokio::time::sleep(Duration::from_millis(50)).await;
                            } else {
                                log.debug("✅ Successfully prefetched next work", Some("Miner"));
                            }
                        }
                        
                        tokio::time::sleep(Duration::from_millis(5)).await;
                    }
                }
            });
            
            // Number of consecutive mining batches to run before yielding
            // Higher values = less context switch overhead, but less responsive to new work
            const BATCHES_PER_YIELD: u32 = 8;
            
            loop {
                // Get work - single lock acquisition to get everything we need
                let work_data = {
                    let mut block_state = server_clone.block_state.lock().await;
                    
                    // Try to use next_block if available
                    if let Some(next_block) = block_state.next_block.take() {
                        block_state.current_work = Work::from_header(next_block.header, next_block.target);
                        block_state.current_block = Some(next_block);
                    }
                    
                    // Reset nonce index if it's getting high
                    if block_state.current_work.nonce_idx > 1000 {
                        block_state.current_work.nonce_idx = 0;
                    }
                    
                    // Get work and block if available
                    block_state.current_block.as_ref().map(|block| {
                        (block_state.current_work.clone(), block.clone())
                    })
                };
                
                // If no work available, wait briefly and retry
                let (mut work, current_block) = match work_data {
                    Some(data) => data,
                    None => {
                        tokio::time::sleep(Duration::from_millis(10)).await;
                        continue;
                    }
                };
                
                // Run multiple mining batches in a single spawn_blocking call
                // This dramatically reduces the overhead of task spawning
                let mining_result = tokio::task::spawn_blocking({
                    let inner_server = Arc::clone(&server_clone);
                    move || {
                        let log = inner_server.log();
                        let mut miner = match inner_server.miner.lock() {
                            Ok(guard) => guard,
                            Err(poisoned) => poisoned.into_inner(),
                        };
                        
                        let mut found_nonce: Option<u64> = None;
                        let mut total_nonces: u64 = 0;
                        let mut batches_run: u32 = 0;
                        
                        // Run multiple batches before returning to async context
                        for _ in 0..BATCHES_PER_YIELD {
                            // Generate a new big_nonce for each batch
                            let big_nonce: u64 = {
                                let mut rng = inner_server.rng.lock().unwrap();
                                rng.gen()
                            };
                            work.set_big_nonce(big_nonce);
                            
                            if !miner.has_nonces_left(&work) {
                                // Reset and continue
                                work.nonce_idx = 0;
                            }
                            
                            match miner.find_nonce(&work, log) {
                                Ok(Some(nonce)) => {
                                    found_nonce = Some(nonce);
                                    total_nonces += miner.num_nonces_per_search();
                                    batches_run += 1;
                                    break; // Found a share, return immediately
                                }
                                Ok(None) => {
                                    total_nonces += miner.num_nonces_per_search();
                                    work.nonce_idx += 1;
                                    batches_run += 1;
                                }
                                Err(e) => {
                                    log.error(format!("Mining error: {:?}", e), Some("Miner"));
                                    break;
                                }
                            }
                        }
                        
                        (found_nonce, total_nonces, batches_run, work)
                    }
                })
                .await;
                
                let (found_nonce, total_nonces, _batches_run, updated_work) = match mining_result {
                    Ok(result) => result,
                    Err(err) => {
                        log.error(format!("Task join error: {:?}", err), Some("Miner"));
                        continue;
                    }
                };
                
                // Update metrics atomically (no lock needed)
                if total_nonces > 0 {
                    server_clone.metrics_nonces.fetch_add(total_nonces, Ordering::Relaxed);
                    
                    // Update work state with new nonce_idx
                    let mut block_state = server_clone.block_state.lock().await;
                    block_state.current_work.nonce_idx = updated_work.nonce_idx;
                }
                
                // Handle found nonce
                if let Some(nonce) = found_nonce {
                    let mut submit_work = updated_work.clone();
                    submit_work.set_big_nonce(nonce);
                    log.info(format!("💎 Block hash below target with nonce: {}", nonce), Some("Share"));
                    
                    // Fetch new work immediately after finding a share
                    let fetch_server = Arc::clone(&server_clone);
                    tokio::spawn(async move {
                        if let Err(err) = update_next_block(&fetch_server).await {
                            fetch_server.log().error(format!("Failed to update next block after share: {:?}", err), Some("Miner"));
                        }
                    });
                    
                    // Submit the block/share asynchronously
                    let mut submit_block_data = current_block;
                    submit_block_data.header = *submit_work.header();
                    
                    let submit_server = Arc::clone(&server_clone);
                    tokio::spawn(async move {
                        if let Err(err) = submit_block(&submit_server, &submit_block_data).await {
                            submit_server.log().error(format!(
                                "submit_block error: {:?}. This could be a connection issue.",
                                err
                            ), Some("Miner"));
                        }
                    });
                }
                
                // Report hashrate if needed (fast check with std::sync::Mutex)
                let should_report = {
                    let timestamp = server_clone.metrics_timestamp.lock().unwrap();
                    SystemTime::now().duration_since(*timestamp)
                        .map(|elapsed| elapsed > server_clone.report_hashrate_interval)
                        .unwrap_or(false)
                };
                
                if should_report {
                    let hashrate = server_clone.calculate_moving_average_hashrate().await;
                    log.report_hashrate(hashrate);
                    let mut timestamp = server_clone.metrics_timestamp.lock().unwrap();
                    *timestamp = SystemTime::now();
                }
                
                // Minimal yield to allow prefetcher to update work if needed
                tokio::task::yield_now().await;
            }
        });
    }
    
    // For solo mining or if we don't want to use the continuous worker
    if !pool_mining {
        loop {
            // Log work state to debug performance
            {
                let block_state = server.block_state.lock().await;
                let has_current = block_state.current_block.is_some();
                let has_next = block_state.next_block.is_some();
                
                log.debug(
                    format!("🧠 Work state: current_block={}, next_block={}", 
                            if has_current { "ready" } else { "empty" },
                            if has_next { "ready" } else { "empty" }),
                    Some("Miner")
                );
            }
            
            // Get the current block to mine on
            let (current_work, has_next_block) = {
                let mut block_state = server.block_state.lock().await;
                
                // If we have a next_block, use it
                if let Some(next_block) = block_state.next_block.take() {
                    block_state.current_work = Work::from_header(next_block.header, next_block.target);
                    block_state.current_block = Some(next_block);
                    log.debug("🛰️ Set current_block from next_block in mining loop", Some("RPC"));
                }
                
                // Check if we have a current block to mine on
                if block_state.current_block.is_none() {
                    // No current block, so we need to get one
                    drop(block_state);
                    log.info("⏳ No work available, fetching immediately...", Some("Miner"));
                    if let Err(err) = update_next_block(&server).await {
                        log.error(format!("Failed to update next block: {:?}", err), Some("Miner"));
                        tokio::time::sleep(Duration::from_millis(100)).await;
                    }
                    continue; // Try again
                }
                
                // We have a current block, let's mine on it
                (block_state.current_work.clone(), block_state.next_block.is_some())
            };
            
            // Immediately trigger fetching the next block if we don't have one
            if !has_next_block {
                let server_clone = Arc::clone(&server);
                tokio::spawn(async move {
                    let log = server_clone.log();
                    log.debug("🔄 Proactively prefetching next work while mining current block", Some("Miner"));
                    if let Err(err) = update_next_block(&server_clone).await {
                        log.error(format!("Proactive prefetch failed: {:?}", err), Some("Miner"));
                    }
                });
            }
            
            // Mine on the current work
            let mut work = current_work;
            
            // Safety check to prevent nonce exhaustion
            if work.nonce_idx > 1000 {
                log.debug("♻️ Resetting high nonce index to prevent exhaustion", Some("Miner"));
                work.nonce_idx = 0;
                
                // Also update the block state to maintain consistency
                let mut block_state = server.block_state.lock().await;
                if let Some(_) = &block_state.current_block {
                    block_state.current_work.nonce_idx = 0;
                }
            }
            
            // Generate big_nonce using std::sync::Mutex (fast, no await needed)
            let big_nonce = {
                let mut rng = server.rng.lock().unwrap();
                rng.gen()
            };
            work.set_big_nonce(big_nonce);
            
            // Run the mining operation on the GPU
            let mining_result = tokio::task::spawn_blocking({
                let server = Arc::clone(&server);
                move || {
                    let log = server.log();
                    let mut miner = match server.miner.lock() {
                        Ok(guard) => guard,
                        Err(poisoned) => poisoned.into_inner(),
                    };
                    if !miner.has_nonces_left(&work) {
                        log.error(format!(
                            "Error: Exhaustively searched nonces. This could be fixed by lowering \
                                   rpc_poll_interval."
                        ), Some("Miner"));
                        return Ok((None, 0));
                    }
                    miner
                        .find_nonce(&work, server.log())
                        .map(|nonce| (nonce, miner.num_nonces_per_search()))
                }
            })
            .await;
            
            // Handle the mining result
            let (nonce, num_nonces_per_search) = match mining_result {
                Ok(Ok((nonce, num_nonces))) => (nonce, num_nonces),
                Ok(Err(err)) => {
                    log.error(format!("Mining error: {:?}", err), Some("Miner"));
                    (None, 0)
                },
                Err(err) => {
                    log.error(format!("Task join error: {:?}", err), Some("Miner"));
                    (None, 0)
                }
            };
            
            // Handle found nonce (share/block)
            if let Some(nonce) = nonce {
                work.set_big_nonce(nonce);
                log.info(format!("💎 Block hash below target with nonce: {}", nonce), Some("Share"));
                
                // Get the block for submission
                let block = {
                    let mut block_state = server.block_state.lock().await;
                    if let Some(mut block) = block_state.current_block.take() {
                        block.header = *work.header();
                        Some(block)
                    } else {
                        log.bug("Bug: Found nonce but no block! Contact the developers.", Some("Share"));
                        None
                    }
                };
                
                // Submit the block/share
                if let Some(block) = block {
                    if let Err(err) = submit_block(&server, &block).await {
                        log.error(format!(
                            "submit_block error: {:?}. This could be a connection issue.",
                            err
                        ), Some("Miner"));
                    }
                }
                
                break; // For solo mining, we break after finding a block
            }
            
            // Update statistics
            {
                let mut block_state = server.block_state.lock().await;
                block_state.current_work.nonce_idx += 1;
                server.metrics_nonces.fetch_add(num_nonces_per_search, Ordering::AcqRel);
            }
            
            // Update and report hashrate if needed - use std::sync::Mutex (fast, no await)
            // Check if enough time has elapsed to report hashrate
            let should_report = {
                let timestamp = server.metrics_timestamp.lock().unwrap();
                SystemTime::now().duration_since(*timestamp)
                    .map(|elapsed| elapsed > server.report_hashrate_interval)
                    .unwrap_or(false)
            };
            
            if should_report {
                let hashrate = server.calculate_moving_average_hashrate().await;
                log.report_hashrate(hashrate);
                // Update timestamp after reporting
                let mut timestamp = server.metrics_timestamp.lock().unwrap();
                *timestamp = SystemTime::now();
            }
            
            // For solo mining, we break here
            break;
        }
    } else {
        // For pool mining, we don't return until the program is terminated
        // Since the worker thread handles all the mining, just wait here
        loop {
            tokio::time::sleep(Duration::from_secs(1)).await;
        }
    }
    
    Ok(())
}
