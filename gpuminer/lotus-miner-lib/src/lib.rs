mod block;
mod miner;
pub mod settings;
mod sha256;
pub mod logger;

use eyre::Result;
pub use miner::Miner;
pub use settings::ConfigSettings;
pub use logger::{Log, LogSeverity, HashrateEntry, LoggerConfig, init_global_logger, LogEntry};

use std::{
    convert::TryInto,
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
    rng: Mutex<rand::rngs::StdRng>,
    metrics_timestamp: Mutex<SystemTime>,
    metrics_nonces: AtomicU64,
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
    pub fn from_config(config: ConfigSettings, report_hashrate_interval: Duration) -> Self {
        let mining_settings = MiningSettings {
            local_work_size: 256,
            inner_iter_size: 16,
            kernel_size: 1 << config.kernel_size,
            kernel_name: "lotus_og".to_string(),
            sleep: 0,
            gpu_indices: vec![config.gpu_index as usize],
        };
        let miner = Miner::setup(mining_settings.clone()).unwrap();
        Server {
            miner: std::sync::Mutex::new(miner),
            client: reqwest::Client::new(),
            node_settings: Mutex::new(NodeSettings {
                bitcoind_url: config.rpc_url.clone(),
                bitcoind_user: config.rpc_user.clone(),
                bitcoind_password: config.rpc_password.clone(),
                rpc_poll_interval: config.rpc_poll_interval.try_into().unwrap(),
                miner_addr: config.mine_to_address.clone(),
                pool_mining: config.pool_mining,
            }),
            block_state: Mutex::new(BlockState {
                current_work: Work::default(),
                current_block: None,
                next_block: None,
                extra_nonce: 0,
            }),
            rng: Mutex::new(rand::rngs::StdRng::from_entropy()),
            metrics_timestamp: Mutex::new(SystemTime::now()),
            metrics_nonces: AtomicU64::new(0),
            log: Log::new(),
            report_hashrate_interval,
        }
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
                    tokio::time::sleep(Duration::from_micros(3)).await;
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
        self.miner.lock().unwrap()
    }

    pub fn log(&self) -> &Log {
        &self.log
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
    log.debug(format!("🛰️ [DEBUG] RPC call: getrawunsolvedblock to URL: {}", url), Some("RPC"));
    let response = init_request(&server)
        .await
        .body(format!(
            r#"{{"method":"getrawunsolvedblock","params":["{}"]}}"#,
            server.node_settings.lock().await.miner_addr
        ))
        .send()
        .await?;
    log.debug(format!("🛰️ [DEBUG] RPC response status: {} for getrawunsolvedblock", response.status()), Some("RPC"));
    let status = response.status();
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
    let mut block_state = server.block_state.lock().await;
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
    let block = create_block(&unsolved_block);
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
    log.debug("🛰️ Set next_block after fetching new work", Some("RPC"));
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
    
    // Extract all needed data from node_settings first, without holding the lock during request
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
    // Format the block hex
    let block_hex = hex::encode(&serialized_block);
    
    // Prepare request body JSON - always include miner_addr as the 2nd parameter when pool mining
    // The server accepts 2 params for BIP22 compatibility, with 2nd param normally ignored
    let request_body = if !pool_mining {
        format!(
            r#"{{"jsonrpc":"2.0","id":"miner","method":"submitblock","params":["{}"]}}"#,
            block_hex
        )
    } else {
        // For pool mining, pass the miner address as second parameter
        format!(
            r#"{{"jsonrpc":"2.0","id":"miner","method":"submitblock","params":["{}", "{}"]}}"#,
            block_hex,
            miner_addr
        )
    };
    
    log.debug(format!("🛰️ [DEBUG] RPC call: submitblock to URL: {}", url), Some("RPC"));
    
    // Create request without using init_request which would try to lock node_settings again
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
    
    // Attempt to parse the response
    let response: Result<SubmitBlockResponse, _> = serde_json::from_str(&response_text);
    
    match response {
        Ok(parsed) => {
            match parsed.result {
                None => {
                    // Check if there was an error
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
    
    // Add a debug log to confirm we've completed the share submission
    log.info("✅ Share submission process completed, resuming mining...", Some("Miner"));
    
    Ok(())
}

async fn mine_some_nonces(server: ServerRef) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let log = server.log();
    let pool_mining = server.node_settings.lock().await.pool_mining;
    
    log.info(format!("🚀 Starting mining loop (pool mode: {})", if pool_mining { "enabled" } else { "disabled" }), Some("Miner"));
    
    loop {
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
                if let Err(err) = update_next_block(&server).await {
                    log.error(format!("Failed to update next block: {:?}", err), Some("Miner"));
                    tokio::time::sleep(Duration::from_millis(100)).await;
                }
                continue; // Try again
            }
            
            // We have a current block, let's mine on it
            (block_state.current_work.clone(), block_state.next_block.is_some())
        };
        
        // Mine on the current work
        let mut work = current_work;
        let big_nonce = server.rng.lock().await.gen();
        work.set_big_nonce(big_nonce);
        
        // Run the mining operation on the GPU
        let (nonce, num_nonces_per_search) = tokio::task::spawn_blocking({
            let server = Arc::clone(&server);
            move || {
                let log = server.log();
                let mut miner = server.miner.lock().unwrap();
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
        .await
        .map_err(|e| Box::new(e) as Box<dyn std::error::Error + Send + Sync>)??;
        
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
            
            // For pool mining, get new work right after finding a share
            if pool_mining {
                log.info("⚡ Share submitted, requesting fresh work...", Some("Miner"));
                if let Err(err) = update_next_block(&server).await {
                    log.error(format!("Failed to update next block after share: {:?}", err), Some("Miner"));
                }
            }
            
            // Continue the mining loop
            continue;
        }
        
        // Update statistics
        {
            let mut block_state = server.block_state.lock().await;
            block_state.current_work.nonce_idx += 1;
            server.metrics_nonces.fetch_add(num_nonces_per_search, Ordering::AcqRel);
        }
        
        // Update and report hashrate if needed
        {
            let mut timestamp = server.metrics_timestamp.lock().await;
            let elapsed = match SystemTime::now().duration_since(*timestamp) {
                Ok(elapsed) => elapsed,
                Err(err) => {
                    log.bug(format!("Bug: Elapsed time error: {}. Contact the developers.", err), Some("Miner"));
                    return Ok(());
                }
            };
            
            if elapsed > server.report_hashrate_interval {
                let num_nonces = server.metrics_nonces.load(Ordering::Acquire);
                let hashrate = num_nonces as f64 / elapsed.as_secs_f64();
                log.report_hashrate(hashrate);
                server.metrics_nonces.store(0, Ordering::Release);
                *timestamp = SystemTime::now();
            }
        }
        
        // For solo mining, break after one attempt
        if !pool_mining {
            break;
        }
        
        // For pool mining, check if we need to get new work based on interval
        // Only request new work if we don't already have a next_block
        if pool_mining && !has_next_block {
            let interval = server.node_settings.lock().await.rpc_poll_interval;
            if interval <= 1 {
                // For very short intervals, request new work on every iteration
                if let Err(err) = update_next_block(&server).await {
                    log.error(format!("Failed to update next block on interval: {:?}", err), Some("Miner"));
                }
            }
        }
    }
    
    Ok(())
}
