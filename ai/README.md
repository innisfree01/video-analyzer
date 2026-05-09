# AI Event Understanding & Daily Summary

Two AI-driven scenarios on top of the existing 4-channel UVC + MediaMTX stack:

1. **Why was this alarm triggered?** — when motion is detected on any channel,
   capture 3 keyframes and let a local Vision-Language Model (Qwen2.5-VL-3B
   via Ollama) explain in natural language what is happening.
2. **What happened today?** — every night, run a small LLM (Qwen2.5-7B) over
   the day's events to produce a digest with highlights and a narrative.

All inference is local; no data leaves the AGX.

## Architecture

```
4× RTSP (MediaMTX :8554)
       │
       ├─► event_engine.py ─► snapshots (NVMe) + events.db (status='pending')
       │                                              │
       │                                              ▼
       │                                       vlm_worker.py ─► Ollama Qwen2.5-VL
       │                                              │
       │                                       UPDATE description/severity
       │                                              │
       └─► (record fmp4 to NVMe, see streaming/mediamtx.yml)
                                                      │
                                                      ▼
                                              api_server.py (:8000)
                                                      │
                                                ┌─────┴─────┐
                                                ▼           ▼
                                            web/        summary_job.py
                                          (Vue VMS)    (cron 23:30)
```

## One-time setup on the AGX

```bash
# 1. Install Ollama (with built-in CUDA support)
curl -fsSL https://ollama.com/install.sh | sh
sudo systemctl enable --now ollama

# 2. Pull models (VLM required; 7B LLM optional if you set llm_model to match vlm)
ollama pull qwen2.5vl:3b                   # VLM + daily summary (text-only /api/chat), ~3 GB
# Optional second model (only if llm_model differs in config.yaml):
# ollama pull qwen2.5:7b-instruct-q4_K_M   # ~5 GB

# 3. Storage on the NVMe SSD
sudo mkdir -p /mnt/nova_ssd/events /mnt/nova_ssd/recordings /mnt/nova_ssd/ai_logs
sudo chown -R "$USER":"$USER" /mnt/nova_ssd/events /mnt/nova_ssd/recordings /mnt/nova_ssd/ai_logs

# 4. Python virtualenv
cd ~/.openclaw/skills/video-analyzer
python3 -m venv ai/.venv
source ai/.venv/bin/activate
pip install -r ai/requirements.txt
```

## Daily ops

```bash
# Restart MediaMTX so the new `record:` config takes effect
bash streaming/stop.sh && bash streaming/start.sh

# Start external-event ingest API only (default; no OpenCV motion detection)
bash ai/start.sh

# Optional legacy/local modes:
# bash ai/start.sh --with-opencv   # local MOG2 motion detection
# bash ai/start.sh --with-vlm      # analyze pending events with VLM

# Tail logs
tail -F ai/logs/*.out

# Stop everything
bash ai/stop.sh

# Generate yesterday's report manually
source ai/.venv/bin/activate
python ai/summary_job.py --date $(date -d 'yesterday' +%F)

# Or run the scheduler daemon (no system cron needed)
nohup python ai/summary_job.py --daemon >ai/logs/summary_job.out 2>&1 &
```

## Verifying end-to-end

```bash
# 1. Liveness
curl http://localhost:18000/health

# 2. Today's events
curl "http://localhost:18000/events?date=$(date +%F)" | python3 -m json.tool

# 3. Trigger a synthetic IPC event; AGX captures one current RTSP snapshot.
curl -X POST http://localhost:18000/events/external \
     -H 'content-type: application/json' \
     -d '{"channel":"cam0","event_type":"human","severity":"high","tags":["human"]}'

# 4. Today's summary (lazy-aggregated if no LLM run yet)
curl http://localhost:18000/summary | python3 -m json.tool

# 5. Browse the UI
# Open http://192.168.3.34:3000/events  and  /summary
```

## Configuration cheatsheet ([ai/config.yaml](config.yaml))

| Key | What it does |
|---|---|
| `motion.fps` | Frames per second processed by MOG2 (downsamples the RTSP stream) |
| `motion.min_area_ratio` | Foreground pixel ratio that counts as "motion" |
| `motion.trigger_frames` | Consecutive qualifying frames required to fire |
| `motion.cooldown_sec` | Per-channel debounce; raise it if you get spam |
| `storage.snapshot_dir` | Where keyframes are written (NVMe recommended) |
| `ollama.vlm_model` | Vision-language model name (must be `ollama pull`-ed) |
| `ollama.llm_model` | Daily-summary model name |
| `ollama.summary_timeout_sec` | Optional longer timeout for `summary_job` (defaults to `timeout_sec`) |
| `ollama.summary_num_predict` | Max tokens for daily summary chat (smaller = faster) |
| `ollama.summary_max_events_in_prompt` | Cap events sent to the LLM; full list still appears in DB fallback if the model fails |
| `mediamtx.playback_base` | Public URL the browser uses for clip playback |

## Troubleshooting

* **`event_engine` cannot open RTSP** — make sure `bash streaming/start.sh`
  succeeded and `curl http://localhost:9997/v3/paths/list` shows ready=true.
* **`vlm_worker` hangs on first event** — the VLM is paging into VRAM.
  First call typically takes 10-30 s; subsequent calls 1-2 s.
* **Lots of low-severity events** — bump `motion.min_area_ratio` from 0.01
  to 0.02 or `motion.cooldown_sec` from 30 to 60.
* **Clip playback returns 404** — verify the MediaMTX version supports
  `recordFormat: fmp4` + the playback server (port 9996). Check with
  `curl http://localhost:9996/list?path=cam0`.
* **Disk filling up** — `recordDeleteAfter: 168h` rotates recordings.
  Snapshots in `events/<date>/` are not auto-pruned yet; add a cron job
  if you need it.
