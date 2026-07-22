use rusqlite::{params, Connection, Result as SqlResult};
use std::path::Path;
use std::sync::Mutex;
use crate::models::{Item, ItemStat, StashLocation};

pub struct Database {
    conn: Mutex<Connection>,
}

impl Database {
    /// Open or create database at the given path
    pub fn open<P: AsRef<Path>>(path: P) -> SqlResult<Self> {
        let conn = Connection::open(path)?;
        let db = Database {
            conn: Mutex::new(conn),
        };
        db.init_tables()?;
        Ok(db)
    }

    /// Initialize database tables
    fn init_tables(&self) -> SqlResult<()> {
        let conn = self.conn.lock().unwrap();
        conn.execute_batch(
            "
            CREATE TABLE IF NOT EXISTS items (
                id INTEGER PRIMARY KEY,
                record TEXT NOT NULL,
                character_name TEXT NOT NULL,
                stash_tab INTEGER NOT NULL,
                slot_x INTEGER NOT NULL,
                slot_y INTEGER NOT NULL,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE(character_name, stash_tab, slot_x, slot_y)
            );

            CREATE TABLE IF NOT EXISTS item_stats (
                id INTEGER PRIMARY KEY,
                item_id INTEGER NOT NULL,
                record TEXT NOT NULL,
                stat TEXT NOT NULL,
                value REAL NOT NULL,
                text_value TEXT,
                FOREIGN KEY(item_id) REFERENCES items(id) ON DELETE CASCADE
            );

            CREATE INDEX IF NOT EXISTS idx_items_character ON items(character_name);
            CREATE INDEX IF NOT EXISTS idx_items_stash ON items(stash_tab);
            CREATE INDEX IF NOT EXISTS idx_item_stats_item ON item_stats(item_id);
            "
        )?;
        Ok(())
    }

    /// Insert or update an item with its stats
    pub fn insert_item(&self, item: &Item, stats: &[ItemStat]) -> SqlResult<i64> {
        let conn = self.conn.lock().unwrap();
        
        // Insert or replace the item
        let _item_id = conn.execute(
            "INSERT OR REPLACE INTO items (record, character_name, stash_tab, slot_x, slot_y)
             VALUES (?, ?, ?, ?, ?)",
            params![
                &item.record,
                &item.stash_location.character_name,
                item.stash_location.stash_tab,
                item.stash_location.slot_x,
                item.stash_location.slot_y,
            ],
        )?;

        let item_id = conn.last_insert_rowid();

        // Delete old stats for this item
        conn.execute(
            "DELETE FROM item_stats WHERE item_id = ?",
            params![item_id],
        )?;

        // Insert new stats
        for stat in stats {
            conn.execute(
                "INSERT INTO item_stats (item_id, record, stat, value, text_value)
                 VALUES (?, ?, ?, ?, ?)",
                params![
                    item_id,
                    &stat.record,
                    &stat.stat,
                    stat.value,
                    &stat.text_value,
                ],
            )?;
        }

        Ok(item_id)
    }

    /// Get all items for a character
    pub fn get_items_by_character(&self, character_name: &str) -> SqlResult<Vec<(Item, Vec<ItemStat>)>> {
        let conn = self.conn.lock().unwrap();
        let mut stmt = conn.prepare(
            "SELECT id, record, character_name, stash_tab, slot_x, slot_y FROM items WHERE character_name = ?"
        )?;

        let items = stmt.query_map(params![character_name], |row| {
            Ok((
                row.get::<_, i64>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, i32>(3)?,
                row.get::<_, i32>(4)?,
                row.get::<_, i32>(5)?,
            ))
        })?;

        let mut result = Vec::new();
        for item_row in items {
            let (item_id, record, char_name, stash_tab, slot_x, slot_y) = item_row?;
            let item = Item {
                id: item_id,
                record,
                stash_location: StashLocation {
                    character_name: char_name,
                    stash_tab,
                    slot_x,
                    slot_y,
                },
            };

            // Get stats for this item
            let mut stat_stmt = conn.prepare(
                "SELECT id, item_id, record, stat, value, text_value FROM item_stats WHERE item_id = ?"
            )?;
            let stats = stat_stmt.query_map(params![item_id], |stat_row| {
                Ok(ItemStat {
                    id: stat_row.get(0)?,
                    item_id: stat_row.get(1)?,
                    record: stat_row.get(2)?,
                    stat: stat_row.get(3)?,
                    value: stat_row.get(4)?,
                    text_value: stat_row.get(5)?,
                })
            })?;

            let stats_vec: Vec<ItemStat> = stats.collect::<SqlResult<Vec<_>>>()?;
            result.push((item, stats_vec));
        }

        Ok(result)
    }

    /// Get all items
    pub fn get_all_items(&self) -> SqlResult<Vec<(Item, Vec<ItemStat>)>> {
        let conn = self.conn.lock().unwrap();
        let mut stmt = conn.prepare(
            "SELECT id, record, character_name, stash_tab, slot_x, slot_y FROM items"
        )?;

        let items = stmt.query_map([], |row| {
            Ok((
                row.get::<_, i64>(0)?,
                row.get::<_, String>(1)?,
                row.get::<_, String>(2)?,
                row.get::<_, i32>(3)?,
                row.get::<_, i32>(4)?,
                row.get::<_, i32>(5)?,
            ))
        })?;

        let mut result = Vec::new();
        for item_row in items {
            let (item_id, record, char_name, stash_tab, slot_x, slot_y) = item_row?;
            let item = Item {
                id: item_id,
                record,
                stash_location: StashLocation {
                    character_name: char_name,
                    stash_tab,
                    slot_x,
                    slot_y,
                },
            };

            // Get stats for this item
            let mut stat_stmt = conn.prepare(
                "SELECT id, item_id, record, stat, value, text_value FROM item_stats WHERE item_id = ?"
            )?;
            let stats = stat_stmt.query_map(params![item_id], |stat_row| {
                Ok(ItemStat {
                    id: stat_row.get(0)?,
                    item_id: stat_row.get(1)?,
                    record: stat_row.get(2)?,
                    stat: stat_row.get(3)?,
                    value: stat_row.get(4)?,
                    text_value: stat_row.get(5)?,
                })
            })?;

            let stats_vec: Vec<ItemStat> = stats.collect::<SqlResult<Vec<_>>>()?;
            result.push((item, stats_vec));
        }

        Ok(result)
    }

    /// Clear all items (useful for testing)
    pub fn clear_all(&self) -> SqlResult<()> {
        let conn = self.conn.lock().unwrap();
        conn.execute("DELETE FROM item_stats", [])?;
        conn.execute("DELETE FROM items", [])?;
        Ok(())
    }
}
