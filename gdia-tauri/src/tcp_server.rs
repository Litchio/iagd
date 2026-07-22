use anyhow::Result;
use serde_json::json;
use std::sync::Arc;
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::net::{TcpListener, TcpStream};
use crate::database::Database;
use crate::models::{Item, ItemStat, StashLocation, IncomingMessage};

pub struct TcpServer {
    addr: String,
    db: Arc<Database>,
}

impl TcpServer {
    pub fn new(addr: String, db: Arc<Database>) -> Self {
        TcpServer { addr, db }
    }

    /// Start the TCP server (runs indefinitely)
    pub async fn run(&self) -> Result<()> {
        let listener = TcpListener::bind(&self.addr).await?;
        println!("[TCP] Server listening on {}", self.addr);

        loop {
            let (socket, addr) = listener.accept().await?;
            println!("[TCP] New connection from: {}", addr);

            let db = Arc::clone(&self.db);
            tokio::spawn(async move {
                if let Err(e) = Self::handle_connection(socket, db).await {
                    eprintln!("[TCP] Connection error: {}", e);
                }
            });
        }
    }

    /// Handle a single client connection
    async fn handle_connection(socket: TcpStream, db: Arc<Database>) -> Result<()> {
        let (reader, mut writer) = socket.into_split();
        let mut buf_reader = BufReader::new(reader);
        let mut line = String::new();

        loop {
            line.clear();
            let n = buf_reader.read_line(&mut line).await?;
            
            // Connection closed
            if n == 0 {
                println!("[TCP] Client disconnected");
                break;
            }

            let line = line.trim();
            if line.is_empty() {
                continue;
            }

            println!("[TCP] Received: {}", line);

            // Try to parse and store the message
            match Self::process_message(line, Arc::clone(&db)).await {
                Ok(_) => {
                    let response = json!({"status": "ok"}).to_string() + "\n";
                    writer.write_all(response.as_bytes()).await?;
                }
                Err(e) => {
                    eprintln!("[TCP] Error processing message: {}", e);
                    let response = json!({"status": "error", "message": e.to_string()}).to_string() + "\n";
                    writer.write_all(response.as_bytes()).await?;
                }
            }
        }

        Ok(())
    }

    /// Process an incoming message JSON
    async fn process_message(json_str: &str, db: Arc<Database>) -> Result<()> {
        let msg: IncomingMessage = serde_json::from_str(json_str)?;
        println!("[TCP] Parsed message type: {}", msg.message_type);

        // Handle different message types
        match msg.message_type.as_str() {
            "TYPE_GAMEENGINE_UPDATE" => {
                println!("[TCP] Game engine update received");
            }
            "TYPE_CloudGetNumFiles" => {
                println!("[TCP] Cloud file request");
            }
            "TYPE_Stash_Item_BasicInfo" => {
                println!("[TCP] Stash item info received");
                
                // Parse and store item data
                if let Some(item_data) = msg.item_data {
                    println!("[TCP] Item data: {}", item_data);
                    
                    // Extract fields from the JSON object
                    let record = item_data.get("record").and_then(|v| v.as_str());
                    let character_name = item_data.get("character_name").and_then(|v| v.as_str());
                    let stash_tab = item_data.get("stash_tab").and_then(|v| v.as_i64());
                    let slot_x = item_data.get("slot_x").and_then(|v| v.as_i64());
                    let slot_y = item_data.get("slot_y").and_then(|v| v.as_i64());
                    
                    if let (Some(record), Some(character_name), Some(stash_tab), Some(slot_x), Some(slot_y)) = 
                        (record, character_name, stash_tab, slot_x, slot_y) {
                        // Create Item
                        let item = Item {
                            id: 0, // Will be set by database
                            record: record.to_string(),
                            stash_location: StashLocation {
                                character_name: character_name.to_string(),
                                stash_tab: stash_tab as i32,
                                slot_x: slot_x as i32,
                                slot_y: slot_y as i32,
                            },
                        };
                        
                        // Parse stats
                        let mut stats = Vec::new();
                        if let Some(stats_array) = item_data.get("stats").and_then(|v| v.as_array()) {
                            for _idx in 0..stats_array.len() {
                                if let Some(stat_obj) = stats_array.get(_idx) {
                                    let stat_name = stat_obj.get("stat").and_then(|v| v.as_str());
                                    let value = stat_obj.get("value").and_then(|v| v.as_f64());
                                    
                                    if let (Some(stat_name), Some(value)) = (stat_name, value) {
                                        let text_value = stat_obj.get("text_value").and_then(|v| v.as_str()).map(String::from);
                                        stats.push(ItemStat {
                                            id: 0, // Will be set by database
                                            item_id: 0, // Will be set by database
                                            record: record.to_string(),
                                            stat: stat_name.to_string(),
                                            value: value as f32,
                                            text_value,
                                        });
                                    }
                                }
                            }
                        }
                        
                        // Insert into database
                        match db.insert_item(&item, &stats) {
                            Ok(item_id) => {
                                println!("[TCP] ✅ Item stored with ID: {}", item_id);
                                println!("[TCP]    Record: {}", record);
                                println!("[TCP]    Character: {}", character_name);
                                println!("[TCP]    Stats count: {}", stats.len());
                            }
                            Err(e) => {
                                eprintln!("[TCP] ❌ Failed to insert item: {}", e);
                            }
                        }
                    } else {
                        eprintln!("[TCP] ⚠️  Missing required fields in item_data");
                    }
                }
            }
            _ => {
                println!("[TCP] Unknown message type: {}", msg.message_type);
            }
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[tokio::test]
    async fn test_tcp_server_start() {
        // Test that server can start (won't work in test, but shows structure)
        // This is mainly to verify the code compiles
    }
}
