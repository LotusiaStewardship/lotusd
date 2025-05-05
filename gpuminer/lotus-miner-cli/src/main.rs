use std::time::Duration;

use log::{info, error, LevelFilter};
use lotus_miner_lib::{
    logger::{LoggerConfig, init_global_logger},
    ConfigSettings, Server,
};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    // Initialize the logger
    let logger_config = LoggerConfig {
        console_output: true,
        file_output: true,
        log_file_path: Some("lotus-miner.log".into()),
        max_log_entries: 1000,
        max_hashrate_entries: 1000,
        level: LevelFilter::Info,
    };

    // Initialize the global logger
    init_global_logger(logger_config).map_err(|e| Box::new(e) as Box<dyn std::error::Error + Send + Sync>)?;
    info!("Author: Alexandre Guillioud - FrenchBTC");
    info!("🌸 Lotus GPU Miner CLI started");
    
    // Load configuration
    let settings = ConfigSettings::load(true)
        .map_err(|e| Box::new(e) as Box<dyn std::error::Error + Send + Sync>)?;
    info!("✅ Configuration loaded successfully");

    // Start mining
    let report_interval = Duration::from_secs(5);
    info!("⏱️ Reporting hashrate every {} seconds", report_interval.as_secs());

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
