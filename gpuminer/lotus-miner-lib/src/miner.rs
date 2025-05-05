use ocl::{
    builders::{DeviceSpecifier, ProgramBuilder},
    Buffer, Context, Device, Kernel, Platform, Queue,
};
use sha2::Digest;
use std::{
    convert::TryInto,
    sync::atomic::{AtomicU64, Ordering},
    time::Instant,
};
use eyre::Result;
use thiserror::Error;
use log::{debug, info, error};
use chrono::Local;

use crate::{sha256::lotus_hash, block::Block, Log, LogSeverity};

// Counter for successful shares/blocks found
static SHARES_FOUND: AtomicU64 = AtomicU64::new(0);
static HASHES_PROCESSED: AtomicU64 = AtomicU64::new(0);
static MINING_START_TIME: once_cell::sync::Lazy<Instant> = once_cell::sync::Lazy::new(|| Instant::now());

#[derive(Debug, Error)]
pub enum MinerError {
    #[error("Ocl error: {0:?}")]
    Ocl(ocl::Error)
}

#[derive(Debug, Clone)]
pub struct MiningSettings {
    pub local_work_size: i32,
    pub kernel_size: u32,
    pub inner_iter_size: i32,
    pub kernel_name: String,
    pub sleep: u32,
    pub gpu_indices: Vec<usize>,
}

pub struct Miner {
    search_kernel: Kernel,
    header_buffer: Buffer<u32>,
    buffer: Buffer<u32>,
    settings: MiningSettings,
}

#[derive(Debug, Clone, Copy)]
pub struct Work {
    header: [u8; 160],
    target: [u8; 32],
    pub nonce_idx: u32,
}

impl From<ocl::Error> for MinerError {
    fn from(err: ocl::Error) -> Self {
        MinerError::Ocl(err)
    }
}

use self::MinerError::*;

impl Work {
    pub fn from_header(header: [u8; 160], target: [u8; 32]) -> Work {
        Work {
            header,
            target,
            nonce_idx: 0,
        }
    }

    pub fn set_big_nonce(&mut self, big_nonce: u64) {
        self.header[44..52].copy_from_slice(&big_nonce.to_le_bytes());
    }

    pub fn header(&self) -> &[u8; 160] {
        &self.header
    }
}

impl Default for Work {
    fn default() -> Self {
        Work {
            header: [0; 160],
            target: [0; 32],
            nonce_idx: 0,
        }
    }
}

fn mining_runtime_stats() -> String {
    let runtime = MINING_START_TIME.elapsed();
    let hours = runtime.as_secs() / 3600;
    let minutes = (runtime.as_secs() % 3600) / 60;
    let seconds = runtime.as_secs() % 60;
    
    let total_shares = SHARES_FOUND.load(Ordering::Relaxed);
    let total_hashes = HASHES_PROCESSED.load(Ordering::Relaxed);
    
    // Calculate shares per hour
    let shares_per_hour = if runtime.as_secs() > 0 {
        (total_shares as f64 * 3600.0) / runtime.as_secs() as f64
    } else {
        0.0
    };
    
    format!(
        "⏱️ Runtime: {:02}:{:02}:{:02} | 🎯 Shares: {} | 📊 Shares/h: {:.2} | 💯 Total hashes: {}",
        hours, minutes, seconds, total_shares, shares_per_hour, total_hashes
    )
}

impl Miner {
    pub fn setup(settings: MiningSettings) -> Result<Self> {
        info!("Setting up OpenCL miner");
        
        // List platforms to get platform id
        let platforms = Platform::list();
        if platforms.is_empty() {
            return Err(eyre::eyre!("No OpenCL platforms found").into());
        }
        
        info!("🔍 Scanning for OpenCL platforms and devices...");
        for (platform_idx, platform) in platforms.iter().enumerate() {
            let platform_name = platform.name().unwrap_or_else(|_| String::from("<invalid platform>"));
            info!("🖥️ Platform {}: {}", platform_idx, platform_name);
            
            match Device::list_all(platform) {
                Ok(devices) => {
                    for (device_idx, device) in devices.iter().enumerate() {
                        let device_name = device.name().unwrap_or_else(|_| String::from("<invalid device>"));
                        info!("  🎮 Device {}: {}", device_idx, device_name);
                    }
                },
                Err(e) => {
                    error!("❌ Error listing devices for platform {}: {:?}", platform_name, e);
                }
            }
        }
        
        // Find the specified platform/device index
        let mut platform_device = None;
        let mut gpu_index = 0;
        for platform in platforms {
            if let Ok(devices) = Device::list_all(&platform) {
                for device in devices {
                    if settings.gpu_indices.contains(&gpu_index) {
                        platform_device = Some((platform.clone(), device));
                        break;
                    }
                    gpu_index += 1;
                }
            }
            if platform_device.is_some() {
                break;
            }
        }
        
        let (platform, device) = platform_device.ok_or_else(|| eyre::eyre!("No suitable GPU found! Check your GPU index."))?;
        
        let platform_name = platform.name().unwrap_or_else(|_| String::from("Unknown platform"));
        let device_name = device.name().unwrap_or_else(|_| String::from("Unknown device"));
        info!("✅ Selected GPU: {} from platform: {}", device_name, platform_name);
        
        // Create context with the selected device
        let context = Context::builder()
            .platform(platform)
            .devices(DeviceSpecifier::Single(device.clone()))
            .build()?;
            
        debug!("🔧 OpenCL context created successfully");
        
        // Create command queue
        let queue = Queue::new(&context, device, None)?;
        debug!("🔄 Command queue created successfully");
        
        // Setup and build the kernels
        let mut prog_builder = ProgramBuilder::new();
        prog_builder
            .src_file(format!("kernels/{}.cl", settings.kernel_name))
            .cmplr_def("WORKSIZE", settings.local_work_size)
            .cmplr_def("ITERATIONS", settings.inner_iter_size);
        
        // Add device to program
        prog_builder.devices(DeviceSpecifier::Single(device));
        
        info!("🔨 Building OpenCL program...");
        let program = prog_builder.build(&context)?;
        info!("✅ OpenCL program built successfully");
        
        // Create kernel and buffers
        let mut kernel_builder = Kernel::builder();
        kernel_builder
            .program(&program)
            .name("search")
            .queue(queue.clone());
            
        let buffer = Buffer::builder().len(0xff).queue(queue.clone()).build()?;
        let header_buffer = Buffer::builder().len(0xff).queue(queue).build()?;
        
        debug!("🧠 OpenCL buffers allocated successfully");
        
        let search_kernel = kernel_builder
            .arg_named("offset", 0u32)
            .arg_named("partial_header", None::<&Buffer<u32>>)
            .arg_named("output", None::<&Buffer<u32>>)
            .build()?;
            
        info!("🚀 Lotus miner initialized with kernel size {}, ready to mine!", settings.kernel_size);
        
        Ok(Miner {
            search_kernel,
            buffer,
            header_buffer,
            settings,
        })
    }

    pub fn list_device_names() -> Vec<String> {
        let platforms = match Platform::list().len() {
            0 => {
                error!("❌ No OpenCL platforms found! Make sure your GPU drivers are properly installed.");
                vec![]
            },
            _ => Platform::list()
        };
        
        let mut device_names = Vec::new();
        for platform in platforms.iter() {
            let platform_name = platform.name().unwrap_or("<invalid platform>".to_string());
            let devices = match Device::list_all(platform) {
                Ok(devices) => devices,
                Err(e) => {
                    error!("❌ Error listing devices for platform {}: {:?}", platform_name, e);
                    continue;
                }
            };
            
            for device in devices.iter() {
                let device_name = device.name().unwrap_or("<invalid device>".to_string());
                info!("💻 Found device: {} on platform {}", device_name, platform_name);
                device_names.push(format!(
                    "{} - {}",
                    platform_name,
                    device_name
                ));
            }
        }
        
        if device_names.is_empty() {
            error!("⚠️ No OpenCL compatible devices found! Check your GPU drivers.");
        } else {
            info!("🎉 Found {} OpenCL compatible device(s)", device_names.len());
        }
        
        device_names
    }

    pub fn has_nonces_left(&self, work: &Work) -> bool {
        work.nonce_idx
            .checked_mul(self.settings.kernel_size)
            .is_some()
    }

    pub fn num_nonces_per_search(&self) -> u64 {
        self.settings.kernel_size as u64 * self.settings.inner_iter_size as u64
    }

    pub fn find_nonce(&mut self, work: &Work, log: &Log) -> Result<Option<u64>> {
        let base = match work
            .nonce_idx
            .checked_mul(self.num_nonces_per_search().try_into().unwrap())
        {
            Some(base) => base,
            None => {
                log.error(
                    "🚨 Error: Nonce base overflow, skipping. This could be fixed by lowering rpc_poll_interval.",
                );
                return Ok(None);
            }
        };
        
        // Track the time it takes to process this batch
        let batch_start = Instant::now();
        
        let mut partial_header = [0u8; 84];
        partial_header[..52].copy_from_slice(&work.header[..52]);
        partial_header[52..].copy_from_slice(&sha2::Sha256::digest(&work.header[52..]));
        let mut partial_header_ints = [0u32; 21];
        for (chunk, int) in partial_header.chunks(4).zip(partial_header_ints.iter_mut()) {
            *int = u32::from_be_bytes(chunk.try_into().unwrap());
        }
        
        debug!("🧮 Processing nonce batch starting at base: {}", base);
        
        self.header_buffer.write(&partial_header_ints[..]).enq().map_err(Ocl)?;
        self.search_kernel
            .set_arg("partial_header", &self.header_buffer).map_err(Ocl)?;
        self.search_kernel.set_arg("output", &self.buffer).map_err(Ocl)?;
        self.search_kernel.set_arg("offset", base).map_err(Ocl)?;
        let mut vec = vec![0; self.buffer.len()];
        self.buffer.write(&vec).enq().map_err(Ocl)?;
        let cmd = self
            .search_kernel
            .cmd()
            .global_work_size(self.settings.kernel_size);
        unsafe {
            cmd.enq().map_err(Ocl)?;
        }
        self.buffer.read(&mut vec).enq().map_err(Ocl)?;
        
        // Update total hashes processed
        let hashes_in_batch = self.num_nonces_per_search();
        let _current_total = HASHES_PROCESSED.fetch_add(hashes_in_batch, Ordering::Relaxed);
        
        // Calculate batch speed
        let batch_time = batch_start.elapsed();
        let speed = if batch_time.as_secs_f64() > 0.0 {
            hashes_in_batch as f64 / batch_time.as_secs_f64() / 1_000_000.0
        } else {
            0.0
        };
        
        if work.nonce_idx % 100 == 0 {
            debug!("⚡ Batch speed: {:.2} MH/s | {}", speed, mining_runtime_stats());
        }
        
        if vec[0x80] != 0 {
            let mut header = work.header;
            'nonce: for &nonce in &vec[..0x7f] {
                let nonce = nonce.swap_bytes();
                if nonce != 0 {
                    header[44..48].copy_from_slice(&nonce.to_le_bytes());
                    let result_nonce = u64::from_le_bytes(header[44..52].try_into().unwrap());
                    let hash = lotus_hash(&header);
                    let mut candidate_hash = hash;
                    candidate_hash.reverse();
                    
                    log.info(format!(
                        "🔍 Candidate: nonce={}, hash={}",
                        result_nonce,
                        hex::encode(&candidate_hash)
                    ));
                    
                    if hash.last() != Some(&0) {
                        log.bug(
                            "🐞 BUG: found nonce's hash has no leading zero byte. Contact the developers.",
                        );
                    }
                    
                    for (&h, &t) in hash.iter().zip(work.target.iter()).rev() {
                        if h > t {
                            continue 'nonce;
                        }
                        if t > h {
                            // Increment share counter
                            let shares = SHARES_FOUND.fetch_add(1, Ordering::Relaxed) + 1;
                            let _timestamp = Local::now().format("%Y-%m-%d %H:%M:%S");
                            
                            // Celebratory log message with stats
                            log.log_str(
                                format!("💰 FOUND VALID SHARE #{} AT NONCE {} 💰\n🎊 Hash: {} 🎊\n📊 Stats: {}", 
                                    shares, 
                                    result_nonce,
                                    hex::encode(&candidate_hash),
                                    mining_runtime_stats()
                                ), 
                                LogSeverity::Info
                            );
                            
                            return Ok(Some(result_nonce));
                        }
                    }
                }
            }
        }
        Ok(None)
    }

    pub fn set_intensity(&mut self, intensity: i32) {
        self.settings.kernel_size = 1 << intensity;
        info!("🔥 Mining intensity set to {} (kernel size: {})", intensity, self.settings.kernel_size);
    }

    pub fn update_gpu_index(&mut self, gpu_index: i64) -> Result<()> {
        if self.settings.gpu_indices[0] == gpu_index as usize {
            info!("ℹ️ GPU index {} is already selected, no change needed", gpu_index);
            return Ok(());
        }
        
        info!("🔄 Switching to GPU with index {}", gpu_index);
        let mut settings = self.settings.clone();
        settings.gpu_indices = vec![gpu_index.try_into().unwrap()];
        *self = Miner::setup(settings)?;
        info!("✅ Successfully switched to GPU with index {}", gpu_index);
        Ok(())
    }

    pub fn execute_round(&mut self, work: &Work, log: &Log) -> Result<()> {
        debug!("🔄 Executing mining round");
        
        // Create a temporary Block to demonstrate using the body methods
        let tmp_block = Block::empty();
        debug!("Block body size: {}", tmp_block.body_size());
        let _body_bytes = tmp_block.get_body();
        
        // Log some mining information including the body data
        log.info(format!(
            "🧮 Mining with header: {} | Body size: {}",
            hex::encode(&work.header[..16]), // Show part of the header
            tmp_block.body_size()
        ));
        
        // Proceed with standard mining operation
        match self.find_nonce(work, log) {
            Ok(Some(nonce)) => {
                log.info(format!("💎 Found potential solution with nonce: {}", nonce));
            }
            Ok(None) => {
                debug!("⏭️ Round completed without finding a solution");
            }
            Err(e) => {
                log.error(format!("❌ Error during mining: {:?}", e));
            }
        }
        
        Ok(())
    }
}
