use std::time::Duration;
use std::str::FromStr;

use log::{info, error, debug, LevelFilter};
use lotus_miner_lib::{
    logger::{LoggerConfig, init_global_logger},
    ConfigSettings, Server,
    miner::{KernelType, Miner, MiningSettings, Work},
    create_genesis_block, update_genesis_timestamp, update_genesis_nonce, get_current_timestamp,
};
use clap::Parser;

/// 🌸 Lotus GPU Miner CLI
///
/// ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
/// ┃  High-performance OpenCL miner for the Lotus chain  ┃
/// ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛
///
/// Welcome to the Lotus GPU Miner!
///
/// Authors:
///   Alexandre Guillioud - FrenchBTC <alexandre@burnlotus.org>
///   Tobias Ruck <ruck.tobias@gmail.com>
///
/// Example usage:
///   lotus-miner-cli -g 0 -s 25 -o lotus_16PSJNgWFFf14otE17Fp43HhjbkFchk4Xgvwy2X27 -i 1 -a https://burnlotus.org --poolmining
///
/// Features:
///   - Self-contained binary with embedded OpenCL kernel
///   - Zero-stall mining architecture
///   - Hashrate stabilization with 60-second moving average
///   - Multiple kernel options (Lotus OG and POCLBM)
///
/// For more information, visit: https://github.com/Boblepointu/lotusd/blob/master/gpuminer/README.md
#[derive(Parser, Debug)]
#[clap(
    name = "Lotus GPU Miner",
    version,
    author = "Alexandre Guillioud - FrenchBTC <alexandre@burnlotus.org>, Tobias Ruck <ruck.tobias@gmail.com>",
    about = "🌸 High-performance OpenCL miner for the Lotus blockchain.",
    long_about = None,
    after_help = "\
EXAMPLES:\n  lotus-miner-cli -g 0 -s 25 -o lotus_16PSJNgWFFf14otE17Fp43HhjbkFchk4Xgvwy2X27 -i 1 -a https://burnlotus.org --poolmining\n\nKernel Types:\n  - lotus_og (default): Optimized kernel for Lotus mining\n  - poclbm: Alternative kernel based on poclbm project\n\nSelf-contained binary with embedded OpenCL kernels - no external files needed!\n\nFor more information, visit: https://github.com/Boblepointu/lotusd/blob/master/gpuminer/README.md\n\nDeveloped by Alexandre Guillioud (FrenchBTC - alexandre@burnlotus.org) and Tobias Ruck (ruck.tobias@gmail.com).\n"
)]
struct Cli {
    /// Path to a configuration file in TOML format. Overrides defaults if present.
    #[clap(short, long, value_name = "CONFIG", help = "Configuration file (TOML)")]
    config: Option<String>,

    /// GPU index to use for mining (default: 0)
    #[clap(short = 'g', long, value_name = "gpu_index", help = "GPU index to use (default: 0)")]
    gpu_index: Option<i64>,

    /// Kernel size (work batch size, default: 23)
    #[clap(short = 's', long, value_name = "kernel_size", help = "Kernel size (default: 23)")]
    kernel_size: Option<i64>,

    /// Select the OpenCL kernel to use: 'lotus_og' (default) or 'poclbm'
    #[clap(long = "kernel-type", value_name = "kernel_type", help = "OpenCL kernel to use: 'lotus_og' or 'poclbm' (default: lotus_og)")]
    kernel_type: Option<String>,

    /// Address to receive mining rewards (mine-to address)
    #[clap(short = 'o', long, value_name = "mine_to_address", help = "Coinbase Output Address (mine-to address)")]
    mine_to_address: Option<String>,

    /// Password for Lotus RPC authentication
    #[clap(short = 'p', long, value_name = "rpc_password", help = "Lotus RPC password")]
    rpc_password: Option<String>,

    /// How often to poll the Lotus node for new work (seconds)
    #[clap(short = 'i', long, value_name = "rpc_poll_interval", help = "Lotus RPC getblocktemplate poll interval (seconds)")]
    rpc_poll_interval: Option<i64>,

    /// Lotus node RPC URL (e.g. http://127.0.0.1:10604 or https://burnlotus.org)
    #[clap(short = 'a', long, value_name = "rpc_url", help = "Lotus RPC address")]
    rpc_url: Option<String>,

    /// Username for Lotus RPC authentication
    #[clap(short = 'u', long, value_name = "rpc_user", help = "Lotus RPC username")]
    rpc_user: Option<String>,

    /// Enable pool mining mode (submit shares to a pool instead of solo mining)
    #[clap(short = 'm', long = "poolmining", help = "Enable pool mining mode")]
    pool_mining: bool,
    
    /// Enable verbose debugging output (shows detailed RPC logs)
    #[clap(long = "debug", help = "Enable verbose debugging output")]
    debug: bool,
    
    /// Enable genesis block mining mode (mines a new genesis block with current timestamp)
    #[clap(long = "genesis", help = "Enable genesis block mining mode")]
    genesis_mining: bool,
    
    /// Difficulty bits for genesis mining (hex format, e.g., 0x1c100000 for testnet)
    #[clap(long = "genesis-bits", value_name = "bits", help = "Difficulty bits for genesis mining (default: 0x1c100000)")]
    genesis_bits: Option<String>,
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let cli = Cli::parse();

    // Initialize the logger
    let logger_config = LoggerConfig {
        console_output: true,
        file_output: true,
        log_file_path: Some("lotus-miner.log".into()),
        max_log_entries: 1000,
        max_hashrate_entries: 1000,
        level: if cli.debug { LevelFilter::Debug } else { LevelFilter::Info },
    };

    // Initialize the global logger
    init_global_logger(logger_config).map_err(|e| Box::new(e) as Box<dyn std::error::Error + Send + Sync>)?;
    info!("Author: Alexandre Guillioud - FrenchBTC - https://burnlotus.org - alexandre@burnlotus.org - https://github.com/Boblepointu/lotusd");
    info!("🌸 Lotus GPU Miner CLI started");
    
    // Load configuration, giving priority to CLI args
    let mut settings = ConfigSettings::load(true)
        .map_err(|e| Box::new(e) as Box<dyn std::error::Error + Send + Sync>)?;
    if let Some(ref v) = cli.gpu_index { settings.gpu_index = *v; }
    if let Some(ref v) = cli.kernel_size { settings.kernel_size = *v; }
    if let Some(ref v) = cli.kernel_type {
        settings.kernel_type = match KernelType::from_str(v) {
            Ok(kernel_type) => kernel_type,
            Err(e) => {
                error!("Invalid kernel type: {}. Using default instead.", e);
                KernelType::LotusOG
            }
        };
    }
    if let Some(ref v) = cli.mine_to_address { settings.mine_to_address = v.clone(); }
    if let Some(ref v) = cli.rpc_password { settings.rpc_password = v.clone(); }
    if let Some(ref v) = cli.rpc_poll_interval { settings.rpc_poll_interval = *v; }
    if let Some(ref v) = cli.rpc_url { settings.rpc_url = v.clone(); }
    if let Some(ref v) = cli.rpc_user { settings.rpc_user = v.clone(); }
    if cli.pool_mining { settings.pool_mining = true; }
    if cli.genesis_mining { settings.genesis_mining = true; }
    if let Some(ref v) = cli.genesis_bits { settings.genesis_bits = Some(v.clone()); }
    if let Some(ref _v) = cli.config {
        // Optionally, reload config from the specified file (not implemented here for brevity)
        // You can add logic to load from a custom config file if needed.
    }
    info!("✅ Configuration loaded successfully");
    
    // Check if genesis mining mode is enabled
    if settings.genesis_mining {
        info!("🌱 Genesis mining mode enabled!");
        return run_genesis_mining(settings, cli.debug).await;
    }
    
    // Add debug logs for configuration settings
    if cli.debug {
        debug!("🔧 Configuration details:");
        debug!("  - GPU Index: {}", settings.gpu_index);
        debug!("  - Kernel Size: {}", settings.kernel_size);
        debug!("  - Kernel Type: {}", settings.kernel_type);
        debug!("  - Mining Address: {}", settings.mine_to_address);
        debug!("  - RPC URL: {}", settings.rpc_url);
        debug!("  - RPC Poll Interval: {}", settings.rpc_poll_interval);
        debug!("  - Pool Mining: {}", settings.pool_mining);
    }

    // Start mining
    let report_interval = Duration::from_secs(5);
    info!("⏱️ Reporting hashrate every {} seconds (using 60s moving average with 15s warm-up period)", report_interval.as_secs());
    
    if cli.debug {
        info!("🔍 Debug mode enabled - showing detailed RPC logs");
    }

    // Create a handle to capture CTRL+C
    let (tx, rx) = tokio::sync::oneshot::channel();
    let tx = std::sync::Mutex::new(Some(tx));
    ctrlc::set_handler(move || {
        if let Some(tx) = tx.lock().unwrap().take() {
            let _ = tx.send(());
        }
    }).map_err(|e| Box::new(e) as Box<dyn std::error::Error + Send + Sync>)?;

    // Create the mining server
    let server = Server::from_config(settings, report_interval);
    
    // Run the server
    let server_ref = std::sync::Arc::new(server);
    
    // Wait for CTRL+C or run the server directly
    tokio::select! {
        _ = rx => {
            info!("👋 Received shutdown signal, stopping miner...");
        }
        result = server_ref.run() => {
            match result {
                Ok(_) => info!("✅ Mining completed successfully"),
                Err(e) => error!("❌ Mining error: {}", e),
            }
        }
    }

    info!("🔄 Shutting down Lotus GPU Miner...");
    Ok(())
}

/// Genesis mining mode - mines a new genesis block with current timestamp
async fn run_genesis_mining(settings: ConfigSettings, debug: bool) -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    info!("🌱 Starting Genesis Block Mining Mode");
    info!("═══════════════════════════════════════════════════════════════");
    info!("📋 Genesis mining will create a new genesis block with:");
    info!("   - Current timestamp (updated every 30 seconds)");
    info!("   - Testnet-like difficulty target");
    info!("   - Identical transaction structure to lotusd");
    info!("═══════════════════════════════════════════════════════════════");
    
    // Parse genesis bits (default to testnet: 0x1c100000)
    let genesis_bits_str = settings.genesis_bits.as_deref().unwrap_or("0x1c100000");
    let genesis_bits = if genesis_bits_str.starts_with("0x") || genesis_bits_str.starts_with("0X") {
        u32::from_str_radix(&genesis_bits_str[2..], 16)
            .map_err(|e| format!("Invalid genesis bits hex: {}", e))?
    } else {
        genesis_bits_str.parse::<u32>()
            .map_err(|e| format!("Invalid genesis bits: {}", e))?
    };
    
    info!("🎯 Genesis difficulty bits: 0x{:08x}", genesis_bits);
    
    // Calculate target from bits (compact format)
    let target = bits_to_target(genesis_bits);
    info!("🎯 Target hash: {}", hex::encode(&target));
    
    // Get initial timestamp
    let initial_time = get_current_timestamp();
    info!("🕐 Initial timestamp: {} ({})", initial_time, 
          chrono::DateTime::from_timestamp(initial_time as i64, 0).unwrap().format("%Y-%m-%d %H:%M:%S UTC"));
    
    // Create initial genesis block
    let mut genesis_block = create_genesis_block(genesis_bits, initial_time, target);
    info!("✅ Genesis block structure created (header: {} bytes, body: {} bytes)", 
          genesis_block.header.len(), genesis_block.body.len());
    
    // Setup GPU miner
    info!("🔧 Initializing GPU miner...");
    let mining_settings = MiningSettings {
        local_work_size: 256,
        inner_iter_size: 16,
        kernel_size: 1 << settings.kernel_size,
        sleep: 0,
        gpu_indices: vec![settings.gpu_index as usize],
        kernel_type: settings.kernel_type,
    };
    
    let mut miner = Miner::setup(mining_settings.clone())
        .map_err(|e| format!("Failed to setup miner: {:?}", e))?;
    
    info!("✅ GPU miner initialized successfully");
    info!("🚀 Starting genesis mining...");
    info!("═══════════════════════════════════════════════════════════════");
    
    let start_time = std::time::Instant::now();
    let mut total_hashes: u64 = 0;
    let mut last_timestamp_update = std::time::Instant::now();
    let mut mining_rounds: u64 = 0;
    
    loop {
        // Check if we should update the timestamp (every 30 seconds)
        if last_timestamp_update.elapsed() > Duration::from_secs(30) {
            let new_time = get_current_timestamp();
            update_genesis_timestamp(&mut genesis_block.header, new_time);
            last_timestamp_update = std::time::Instant::now();
            info!("🕐 Updated timestamp to: {} ({})", new_time,
                  chrono::DateTime::from_timestamp(new_time as i64, 0).unwrap().format("%Y-%m-%d %H:%M:%S UTC"));
        }
        
        // Create work from genesis block header
        let mut work = Work::from_header(genesis_block.header, target);
        
        // Generate random nonce base
        let nonce_base: u64 = rand::random();
        work.set_big_nonce(nonce_base);
        
        mining_rounds += 1;
        
        // Log periodic status (every 100 rounds or in debug mode)
        if debug || mining_rounds % 100 == 0 {
            let elapsed = start_time.elapsed();
            let hashrate = if elapsed.as_secs() > 0 {
                total_hashes as f64 / elapsed.as_secs() as f64
            } else {
                0.0
            };
            
            info!("⛏️  Round {}: nonce_base={:#018x}, hashes={}, hashrate={:.2} MH/s, runtime={}s",
                  mining_rounds, nonce_base, format_number(total_hashes), 
                  hashrate / 1_000_000.0, elapsed.as_secs());
        }
        
        // Mine on this work
        match miner.find_nonce(&work, &lotus_miner_lib::Log::new()) {
            Ok(Some(found_nonce)) => {
                // Found a solution!
                info!("═══════════════════════════════════════════════════════════════");
                info!("🎉 GENESIS BLOCK FOUND!");
                info!("═══════════════════════════════════════════════════════════════");
                
                // Update the header with the winning nonce
                update_genesis_nonce(&mut genesis_block.header, found_nonce);
                
                // Compute the final block hash
                use lotus_miner_lib::sha256::lotus_hash;
                let block_hash = lotus_hash(&genesis_block.header);
                let mut display_hash = block_hash.clone();
                display_hash.reverse();
                
                info!("✨ Winning nonce: {}", found_nonce);
                info!("🔗 Block hash: {}", hex::encode(&display_hash));
                info!("🕐 Timestamp: {}", get_timestamp_from_header(&genesis_block.header));
                info!("⏱️  Mining time: {:.2} seconds", start_time.elapsed().as_secs_f64());
                info!("💯 Total hashes: {}", format_number(total_hashes + miner.num_nonces_per_search()));
                info!("═══════════════════════════════════════════════════════════════");
                info!("📝 Genesis block parameters for chainparams.cpp:");
                info!("═══════════════════════════════════════════════════════════════");
                info!("genesis = CreateGenesisBlock(0x{:08x}, {}, {}ull);", 
                      genesis_bits, get_timestamp_from_header(&genesis_block.header), found_nonce);
                info!("consensus.hashGenesisBlock = genesis.GetHash();");
                info!("assert(genesis.GetSize() == {});", 160 + genesis_block.body.len());
                info!("assert(consensus.hashGenesisBlock ==");
                info!("       uint256S(\"{}\"));", hex::encode(&display_hash));
                info!("═══════════════════════════════════════════════════════════════");
                
                // Save to file
                let filename = format!("genesis_block_{}.txt", get_timestamp_from_header(&genesis_block.header));
                match std::fs::write(&filename, format!(
                    "Genesis Block Found!\n\
                     ===================\n\
                     \n\
                     Nonce: {}\n\
                     Timestamp: {}\n\
                     Bits: 0x{:08x}\n\
                     Block Hash: {}\n\
                     Block Size: {}\n\
                     \n\
                     C++ Code for chainparams.cpp:\n\
                     ==============================\n\
                     genesis = CreateGenesisBlock(0x{:08x}, {}, {}ull);\n\
                     consensus.hashGenesisBlock = genesis.GetHash();\n\
                     assert(genesis.GetSize() == {});\n\
                     assert(consensus.hashGenesisBlock ==\n\
                            uint256S(\"{}\"));\n\
                     \n\
                     Header (hex): {}\n\
                     Body (hex): {}\n",
                    found_nonce,
                    get_timestamp_from_header(&genesis_block.header),
                    genesis_bits,
                    hex::encode(&display_hash),
                    160 + genesis_block.body.len(),
                    genesis_bits,
                    get_timestamp_from_header(&genesis_block.header),
                    found_nonce,
                    160 + genesis_block.body.len(),
                    hex::encode(&display_hash),
                    hex::encode(&genesis_block.header),
                    hex::encode(&genesis_block.body),
                )) {
                    Ok(_) => info!("💾 Genesis block saved to: {}", filename),
                    Err(e) => error!("❌ Failed to save genesis block: {}", e),
                }
                
                return Ok(());
            }
            Ok(None) => {
                // No solution found in this batch, continue mining
                total_hashes += miner.num_nonces_per_search();
            }
            Err(e) => {
                error!("❌ Mining error: {:?}", e);
                return Err(format!("Mining error: {:?}", e).into());
            }
        }
    }
}

/// Convert compact bits format to full 256-bit target
fn bits_to_target(bits: u32) -> [u8; 32] {
    let exponent = (bits >> 24) as usize;
    let mantissa = bits & 0x00ffffff;
    
    let mut target = [0xffu8; 32];
    
    if exponent <= 3 {
        let value = mantissa >> (8 * (3 - exponent));
        target[29] = (value & 0xff) as u8;
        target[30] = ((value >> 8) & 0xff) as u8;
        target[31] = ((value >> 16) & 0xff) as u8;
    } else {
        let size = exponent - 3;
        if size < 32 {
            // Clear the lower bytes
            for i in 0..(32 - size) {
                target[i] = 0;
            }
            // Set the mantissa bytes
            let offset = 32 - size;
            if offset >= 3 {
                target[offset - 3] = ((mantissa >> 16) & 0xff) as u8;
                target[offset - 2] = ((mantissa >> 8) & 0xff) as u8;
                target[offset - 1] = (mantissa & 0xff) as u8;
            }
        }
    }
    
    target
}

/// Extract timestamp from genesis block header
fn get_timestamp_from_header(header: &[u8; 160]) -> u64 {
    let offset = 32 + 4; // After hashPrevBlock and nBits
    u64::from_le_bytes([
        header[offset],
        header[offset + 1],
        header[offset + 2],
        header[offset + 3],
        header[offset + 4],
        header[offset + 5],
        0,
        0,
    ])
}

/// Format a number with thousand separators
fn format_number(value: u64) -> String {
    let s = value.to_string();
    let mut chars = s.chars().rev().collect::<Vec<_>>();
    for i in (3..chars.len()).step_by(4) {
        chars.insert(i, ',');
    }
    chars.into_iter().rev().collect()
}
