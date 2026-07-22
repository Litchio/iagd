mod models;
mod database;
mod tcp_server;

use anyhow::Result;
use std::sync::Arc;
use std::path::PathBuf;
use database::Database;
use tcp_server::TcpServer;

#[tokio::main]
async fn main() -> Result<()> {
    // Setup logging
    env_logger::Builder::from_default_env()
        .format_timestamp_millis()
        .init();

    println!("🎮 GDIA Backend Server Starting...");

    // Create database directory if it doesn't exist
    let data_dir = PathBuf::from("./data");
    std::fs::create_dir_all(&data_dir)?;
    
    let db_path = data_dir.join("gdia.db");
    println!("📦 Database path: {}", db_path.display());

    // Initialize database
    let db = Arc::new(Database::open(&db_path)?);
    println!("✅ Database initialized");

    // Start TCP server
    let tcp_server = TcpServer::new("127.0.0.1:1337".to_string(), Arc::clone(&db));
    
    // Run TCP server (blocks indefinitely)
    tcp_server.run().await?;

    Ok(())
}
