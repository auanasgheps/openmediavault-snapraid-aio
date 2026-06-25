# Internal Changelog

## 8.1 — 2026-06-25

### Script
- Replaced bundled `snapraid-aio-script.sh` with v3.4 stable release

### Datamodel (`conf.service.snapraidaio.config.json`)
- Added `docker_remote` (boolean) — enable remote Docker management
- Added `docker_user` (string) — SSH user for remote Docker host
- Added `docker_host_services` (string) — newline-separated remote host:container entries
- Added `docker_delay` (integer) — seconds to wait after restarting services

### RPC schema (`rpc.snapraidaio.json`)
- Added the 4 new Docker remote fields to `rpc.snapraidaio.setconfig` to match datamodel

### Jinja2 template (`etc-snapraid-aio_conf.j2`)
- Fixed `APPRISE_URL` — was rendered as a string, now correctly rendered as a Bash array `("...")`
- `EMAIL_ADDRESS` / `FROM_EMAIL_ADDRESS` — now sourced from OMV system email config via `omv_conf.get_by_filter`
- `CHECK_UPDATES` — hardcoded to `0`; updates ship with the package
- `TELEGRAM_*` / `DISCORD_*` — hardcoded disabled; Apprise is the sole notification channel going forward
- Added `DOCKER_REMOTE`, `DOCKER_USER`, `DOCKER_HOST_SERVICES` (Bash array, one entry per line), `DOCKER_DELAY`

### UI form (`omv-services-snapraidaio-configs-form-page.yaml`)
- Split the monolithic "Thresholds and scrub settings" divider into three sections:
  **Sync thresholds**, **Scrub settings**, **SnapRAID options**
- Added "Remote Docker management" section with `docker_remote`, `docker_user`,
  `docker_host_services` (textarea), `docker_delay` — all gated on their enable toggles
- Added descriptive `hint` text to all fields that previously had none:
  `del_threshold`, `up_threshold`, `add_del_threshold`, `sync_warn_threshold`,
  `scrub_percent`, `scrub_age`, `scrub_delayed_run`, `prehash`, `force_zero`,
  `spindown`, `verbosity`, `retention_days`, `snapraid_log_dir`,
  `before_hook_name`, `before_hook_cmd`, `after_hook_name`, `after_hook_cmd`,
  `execute_before_spindown`
- Fixed misleading labels:
  - "Use Apprise" → "Enable Apprise"
  - "Send Apprise email" → "Enable Apprise email"
  - "Force zero-sized file sync" → "Sync zero-sized files"
  - "Use hd-idle spindown" → "Spin down disks when done"
  - "Notify" (snap_status) → "Include status report in notification"
  - "Send SMART log via Telegram/Discord" → "Include SMART log in notification"
  - "Execute before spindown" → "Execute after hook before spindown"

### debian/changelog
- Bumped version to 8.1 with full change summary

## 8.0 — 2025-10-05

- Initial release by ryecoaaron
