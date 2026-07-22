use serde::{Deserialize, Serialize};

/// Represents an item in the game stash
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Item {
    pub id: i64,
    pub record: String,
    pub stash_location: StashLocation,
}

/// Represents the stash location (character, tab, position)
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StashLocation {
    pub character_name: String,
    pub stash_tab: i32,
    pub slot_x: i32,
    pub slot_y: i32,
}

/// Represents a stat on an item
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ItemStat {
    pub id: i64,
    pub item_id: i64,
    pub record: String,
    pub stat: String,
    pub value: f32,
    pub text_value: Option<String>,
}

/// Incoming message from the TCP client
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct IncomingMessage {
    #[serde(rename = "type")]
    pub message_type: String,
    pub timestamp: i64,
    #[serde(default)]
    pub data_length: i32,
    #[serde(default)]
    pub item_data: Option<serde_json::Value>,
}
