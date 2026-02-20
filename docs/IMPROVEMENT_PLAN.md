# ChestTrading Improvement Plan (v2)

Scope: This document focuses on PackChest (pack/unpack) improvements: permission checks + migrating dynamic_pricing and player_limits during pack/unpack.

Status: Design ready (implementation pending).

## Goals
- Pack/unpack is atomic: either everything succeeds or nothing changes.
- Pack/unpack migrates these configs:
  - dynamic_pricing
  - player_limits
- Pack/unpack enforces permissions and prevents other players from restoring someone else\'s packed chest by default.
- Failure handling is safe: no config loss, no item loss, no "half restored" states.

## Non-goals
- Redesign of shop/recycle/dynamic pricing business rules.
- Changing existing pricing/limit semantics.

## Current Risks (Why Change)
- packChest/unpackChest is currently multi-step without a transaction, and does not check every SQL copy step for success.
  - This can create half-completed data and permanent config loss.
- /packchest currently does not enforce a dedicated permission node.
  - Pack/unpack can become a backdoor if not controlled.
- placed/unpack path currently lacks owner validation for packed_id.
  - Anyone who obtains a packed chest item can potentially restore another player\'s shop config.

## Permission Model
Add a new permission node:
- chest.pack: allows using packchest and performing pack/unpack.

Existing:
- chest.admin: admin bypass (already used elsewhere).

Default behavior (recommended):
- Enter/exit pack mode: requires chest.pack.
- Packing a configured chest: requires chest.pack AND (owner == player OR chest.admin).
- Unpacking/restoring: requires chest.pack AND (packed_owner == player OR chest.admin).

Optional behavior (configurable):
- allowTransfer: if true, allow non-owner unpack (still requires chest.pack). Default: false.

## Config Proposal
Add to config.json (names can be adjusted to match project style):

```json
{
  "packChest": {
    "enabled": true,
    "allowUnconfigured": false,
    "allowTransfer": false,
    "pendingTimeoutSec": 30
  }
}
```

Notes:
- allowUnconfigured=false is strongly recommended. Otherwise pack becomes equivalent to removing blocks via code and can bypass other protection systems.

## Database Changes (Schema V9)
Problem: packing deletes the row in chests (cascades to child tables). To migrate dynamic_pricing/player_limits, we must snapshot them to packed_* tables first.

Add two new tables:

```sql
-- V9: packed_dynamic_pricing
CREATE TABLE IF NOT EXISTS packed_dynamic_pricing (
  packed_id INTEGER NOT NULL,
  item_id INTEGER NOT NULL,
  is_shop INTEGER NOT NULL,
  price_tiers TEXT NOT NULL,
  stop_threshold INTEGER NOT NULL DEFAULT -1,
  current_count INTEGER NOT NULL DEFAULT 0,
  reset_interval_hours INTEGER NOT NULL DEFAULT 24,
  last_reset_time INTEGER NOT NULL,
  enabled INTEGER NOT NULL DEFAULT 1,
  PRIMARY KEY (packed_id, item_id, is_shop),
  FOREIGN KEY (packed_id) REFERENCES packed_chests(packed_id) ON DELETE CASCADE,
  FOREIGN KEY (item_id) REFERENCES item_definitions(item_id) ON DELETE CASCADE
);

-- V9: packed_player_limits
CREATE TABLE IF NOT EXISTS packed_player_limits (
  packed_id INTEGER NOT NULL,
  player_uuid TEXT NOT NULL DEFAULT '',
  limit_count INTEGER NOT NULL,
  limit_seconds INTEGER NOT NULL,
  is_shop INTEGER NOT NULL,
  PRIMARY KEY (packed_id, player_uuid, is_shop),
  FOREIGN KEY (packed_id) REFERENCES packed_chests(packed_id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_packed_dp_packed_id ON packed_dynamic_pricing(packed_id);
CREATE INDEX IF NOT EXISTS idx_packed_limits_packed_id ON packed_player_limits(packed_id);
```

Migration implementation:
- Add migrateToV9() in src/db/SchemaMigration.cpp and append it to the migrations list.

## Pack/Unpack Data Flow
### Pack (Configured Chest Only)
Required invariants:
- Must run inside a single SQL transaction.
- Must copy ALL child data before deleting from chests.

Algorithm:
1) Read chests row at (dim,pos).
2) Insert into packed_chests, get packed_id.
3) Copy child tables:
   - shop_items -> packed_shop_items
   - recycle_shop_items -> packed_recycle_items
   - shared_chests -> packed_shared_chests
   - dynamic_pricing -> packed_dynamic_pricing (NEW)
   - player_limits -> packed_player_limits (NEW)
4) Delete from chests (cascades original child data).
5) Commit.

Copy SQL examples:

```sql
INSERT INTO packed_dynamic_pricing
(packed_id, item_id, is_shop, price_tiers, stop_threshold, current_count, reset_interval_hours, last_reset_time, enabled)
SELECT ?, item_id, is_shop, price_tiers, stop_threshold, current_count, reset_interval_hours, last_reset_time, enabled
FROM dynamic_pricing
WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;

INSERT INTO packed_player_limits
(packed_id, player_uuid, limit_count, limit_seconds, is_shop)
SELECT ?, player_uuid, limit_count, limit_seconds, is_shop
FROM player_limits
WHERE dim_id = ? AND pos_x = ? AND pos_y = ? AND pos_z = ?;
```

### Unpack
Required invariants:
- Must run inside a single SQL transaction.
- Must NOT overwrite existing chest config at the target location.

Algorithm:
1) Validate packed_id exists.
2) Validate (newDim,newPos) is not already present in chests.
3) Insert into chests at new location.
4) Restore child data:
   - packed_shop_items -> shop_items
   - packed_recycle_items -> recycle_shop_items
   - packed_shared_chests -> shared_chests
   - packed_dynamic_pricing -> dynamic_pricing (NEW)
   - packed_player_limits -> player_limits (NEW)
5) Delete packed_chests (cascades packed child rows).
6) Commit.

Restore SQL examples:

```sql
INSERT INTO dynamic_pricing
(dim_id, pos_x, pos_y, pos_z, item_id, is_shop, price_tiers, stop_threshold, current_count, reset_interval_hours, last_reset_time, enabled)
SELECT ?, ?, ?, ?, item_id, is_shop, price_tiers, stop_threshold, current_count, reset_interval_hours, last_reset_time, enabled
FROM packed_dynamic_pricing
WHERE packed_id = ?;

INSERT INTO player_limits
(dim_id, pos_x, pos_y, pos_z, player_uuid, limit_count, limit_seconds, is_shop)
SELECT ?, ?, ?, ?, player_uuid, limit_count, limit_seconds, is_shop
FROM packed_player_limits
WHERE packed_id = ?;
```

## Runtime Permission Checks
### packchest command
- In src/command/command.cpp, /packchest execute handler:
  - If origin is not player: error.
  - If !hasPermission(uuid, "chest.pack"): output.error + return.

### packing action (interact)
- In src/interaction/ChestInteractHandler.cpp (tryHandlePackChestMode):
  - Require chest.pack.
  - Require owner/admin for configured chests.

### unpack action (placed)
- In src/interaction/ChestPlaceHandler.cpp:
  - Require chest.pack.
  - Query packed_chests.player_uuid for packed_id.
  - If !admin and packed_owner != playerUuid and !allowTransfer: reject.

## Pending Cache (placing -> placed)
Current approach stores only packedId by player UUID. Improve to store the original item NBT for safe rollback.

Suggested structure:
- key: playerUuid
- value: { packedId, itemSnbt (or binary), timestamp }

Cleanup:
- TTL cleanup at each event: now - ts > pendingTimeoutSec
- on PlayerDisconnect: remove pending entry

## Failure Handling (Unpack)
If any check fails or unpack DB transaction fails:
- Remove the newly placed chest block.
- Refund the original packed chest item (recreate from stored NBT). If inventory full, drop.
- Send an i18n message explaining the reason.

## Floating Text / Fake Item Config After Unpack
After a successful unpack:
- Read chests.enable_floating_text and chests.enable_fake_item from DB.
- If enable_floating_text == 0: do not create floating text.
- If floating text exists and is shop/recycle:
  - ensure per-chest enableFakeItem is applied (ft.enableFakeItem)
  - call updateShopFloatingText to load item list

## Implementation Tasks (By File)
- src/db/SchemaMigration.h / src/db/SchemaMigration.cpp
  - Add migrateToV9
  - Create packed_dynamic_pricing, packed_player_limits

- src/repository/ChestRepository.cpp
  - Make packChest transactional
  - Make unpackChest transactional
  - Copy/restore dynamic_pricing + player_limits
  - Add target-occupied check in unpack
  - Add helper: getPackedOwnerUuid(packedId)

- src/command/command.cpp
  - Add permission check for chest.pack in /packchest

- src/interaction/ChestInteractHandler.cpp
  - Require chest.pack in tryHandlePackChestMode

- src/interaction/ChestPlaceHandler.cpp
  - Store original item NBT in placing
  - Permission + owner checks in placed
  - Rollback placement + refund item on failure
  - Apply floating text settings from DB after success

- lang/zh_CN.json / lang/en_US.json (or default generation)
  - Add missing i18n keys for pack/unpack success/fail reasons

## Test / Acceptance
Minimum acceptance tests:
- Pack/unpack a chest with shop + dynamic_pricing + player_limits:
  - After unpack, dynamic_pricing and player_limits rows exist at new position with identical values.
- Permission:
  - Without chest.pack, cannot pack/unpack.
  - Without admin, cannot unpack someone else\'s packed chest when allowTransfer=false.
- Safety:
  - Unpack to an occupied location fails and refunds the packed item.
  - Unpack failure never consumes the item without restoring.

## Rollout Notes
- Upgrading DB schema to V9: advise users to backup ChestTrading.db.
- Packed chests created before V9 will not contain packed_dynamic_pricing/packed_player_limits snapshots; unpacking those will restore without these extra configs (expected).
