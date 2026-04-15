# OFIQ — Dockerized ICAO/ISO Face Image Quality Validator

Standalone, isolated Docker setup for **OFIQ v1.1.2** — the official reference
implementation of **ISO/IEC 29794-5** by Germany's BSI. Outputs ~21 face image
quality measures plus a Unified Quality Score (UQS) for ICAO 9303 photo
compliance checking.

Upstream: https://github.com/BSI-OFIQ/OFIQ-Project

This project ships **two images**:

| Image | Purpose | Use it for |
|---|---|---|
| `ofiq:1.1.2` | Pure CLI — wraps `OFIQSampleApp` | Ad-hoc batches, scripting, CI |
| `ofiq-api:1.1.2` | Native C++ HTTP server linking `libofiq_lib.so` directly | Browser / REST inference. Models load **once** at startup. ~300 ms per request. |

---

## Quick start (web UI)

```bash
cd E:/projects/OIFQ
docker compose build               # builds both images (ofiq + ofiq-api)
docker compose up -d ofiq-api      # starts only the API
```

Then open <http://localhost:8080> and drop a face photo.

```bash
# JSON via curl
curl -sS -F "file=@samples/face.jpg" http://localhost:8080/v1/validate | jq .
```

Health / metrics endpoints:

```bash
curl http://localhost:8080/healthz   # liveness
curl http://localhost:8080/readyz    # readiness (200 once workers loaded models)
curl http://localhost:8080/version
curl http://localhost:8080/metrics   # Prometheus exposition
```

Stop the API:

```bash
docker compose down
```

---

## CLI (image-by-image batch use)

The CLI image is **not** started by default. Use the `cli` profile:

```bash
docker compose --profile cli run --rm ofiq \
  -c /opt/ofiq/data \
  -i /opt/ofiq/data/tests/images/b-01-smile.png \
  -o /work/output/smoke.csv
```

> **Important CLI gotchas (Phase 1 lessons):**
>
> 1. `-c` must be the **data directory** (`/opt/ofiq/data`), not the `.jaxn`
>    file. OFIQ resolves model paths relative to the data dir.
> 2. On **Git Bash for Windows**, prefix every `docker run` with
>    `MSYS_NO_PATHCONV=1` to stop MSYS from rewriting absolute container paths
>    like `/opt/ofiq/data` into `C:/Program Files/Git/opt/ofiq/data`.

### Plain `docker run` (without compose)

```bash
# smoke test on a bundled conformance image
MSYS_NO_PATHCONV=1 docker run --rm ofiq:1.1.2 \
  -c /opt/ofiq/data \
  -i /opt/ofiq/data/tests/images/b-01-smile.png \
  -o /tmp/result.csv

# validate your own image
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "E:/projects/OIFQ/samples:/work/samples:ro" \
  -v "E:/projects/OIFQ/output:/work/output" \
  ofiq:1.1.2 \
  -c /opt/ofiq/data \
  -i /work/samples/face.jpg \
  -o /work/output/result.csv

# batch a whole directory
MSYS_NO_PATHCONV=1 docker run --rm \
  -v "E:/projects/OIFQ/samples:/work/samples:ro" \
  -v "E:/projects/OIFQ/output:/work/output" \
  ofiq:1.1.2 \
  -c /opt/ofiq/data \
  -i /work/samples \
  -o /work/output/batch.csv
```

CSV columns include `UnifiedQualityScore`, `BackgroundUniformity`,
`IlluminationUniformity`, `LuminanceMean`, `LuminanceVariance`,
`UnderExposurePrevention`, `OverExposurePrevention`, `DynamicRange`, `Sharpness`,
`CompressionArtifacts`, `NaturalColour`, `SingleFacePresent`, `EyesOpen`,
`MouthClosed`, `EyesVisible`, `MouthOcclusionPrevention`,
`FaceOcclusionPrevention`, `InterEyeDistance`, `HeadSize`,
`LeftwardCropOfTheFaceImage`, `RightwardCropOfTheFaceImage`,
`MarginAboveOfTheFaceImage`, `MarginBelowOfTheFaceImage`, `HeadPoseYaw`,
`HeadPosePitch`, `HeadPoseRoll`, `ExpressionNeutrality`, `NoHeadCoverings`.
Each measure has a `.native` (raw) and `.scalar` (0–100) column.

---

## REST API (`ofiq-api`)

### Endpoints

| Method | Path | Purpose |
|---|---|---|
| `GET` | `/` | Drag-and-drop HTML page |
| `GET` | `/healthz` | Liveness probe (always 200 once process is up) |
| `GET` | `/readyz` | 200 only when ≥1 worker has loaded all models |
| `GET` | `/version` | `{"server":"0.1.0","ofiq":"1.1.2"}` |
| `GET` | `/metrics` | Prometheus exposition |
| `POST` | `/v1/validate` | multipart `file=@photo.jpg` → JSON result |

### Response shape

```json
{
  "request_id": "r-000000000001",
  "server": "0.1.0",
  "ofiq_version": "1.1.2",
  "image": { "width": 1024, "height": 1280, "channels": 3, "upload_bytes": 198432 },
  "elapsed_ms": 312.4,
  "inference_ms": 305.1,
  "bounding_box": { "x": 256, "y": 180, "width": 512, "height": 640 },
  "measures": {
    "UnifiedQualityScore":  { "raw": 24.33,  "scalar": 63.0, "status": "success" },
    "MouthClosed":          { "raw":  0.002, "scalar": 23.0, "status": "success" },
    "...":                  { }
  },
  "verdict": {
    "icao_compliant": false,
    "uqs": 63,
    "uqs_ok": false,
    "failed": [
      { "measure": "UnifiedQualityScore", "scalar": 63, "threshold": 70, "reason": "below_threshold" },
      { "measure": "MouthClosed",         "scalar": 23, "threshold": 70, "reason": "below_threshold" }
    ],
    "thresholds_count": 26
  }
}
```

### Error codes

| Status | Meaning |
|---|---|
| 400 | `missing_file_field` — multipart upload is missing `file` |
| 413 | `payload_too_large` — exceeds `OFIQ_MAX_UPLOAD_MB` |
| 415 | `decode_failed` — not a recognized JPG/PNG/BMP |
| 500 | `inference_failed` — OFIQ returned an error (face not detected, etc.) |
| 503 | `not_ready` (workers still initializing) or `queue_full` (back-pressure) |
| 504 | `timeout` — inference exceeded `OFIQ_REQUEST_TIMEOUT_MS` |

503 responses include `Retry-After` headers.

### Tuning

All knobs are environment variables:

| Env | Default | Notes |
|---|---|---|
| `OFIQ_WORKERS` | `min(8, nproc)` | Parallel OFIQ instances. RAM ≈ N × 250 MB. |
| `OFIQ_QUEUE_DEPTH` | `256` | Bounded queue. Beyond this, requests get `503 queue_full`. |
| `OFIQ_MAX_UPLOAD_MB` | `10` | Multipart size cap. |
| `OFIQ_REQUEST_TIMEOUT_MS` | `30000` | Per-request hard timeout. |
| `OFIQ_STARTUP_TIMEOUT_MS` | `120000` | How long to wait for workers to initialize before giving up. |
| `OFIQ_THRESHOLDS` | `/etc/ofiq-api/thresholds.json` | ICAO threshold table — see [api/thresholds.json](api/thresholds.json). Edit the file & restart to retune. |
| `OFIQ_LOG_LEVEL` | `info` | `debug` `info` `warn` `error` |
| `OFIQ_HOST` | `0.0.0.0` | Bind address. |
| `OFIQ_PORT` | `8080` | |

### Architecture

```
   browser / client
        │
        ▼
   ┌────────────────────────────────────────────────────┐
   │  ofiq-api container (long-lived)                    │
   │    cpp-httplib HTTP/1.1 server  ──┐                 │
   │                                    ▼                │
   │                 bounded MPMC queue (256)            │
   │                                    │                │
   │                       ┌──── worker 0 (OFIQ instance loaded once)
   │                       ├──── worker 1 (OFIQ instance loaded once)
   │                       ├──── ...
   │                       └──── worker N-1
   │                              │
   │                              ▼
   │                     vectorQuality()  ~300 ms
   └────────────────────────────────────────────────────┘
```

- Each worker owns its own `OFIQ::Interface` instance (so OFIQ never sees
  concurrent calls — sidesteps thread-safety questions entirely).
- Models are loaded **once per worker** at process start, not per request.
- HTTP, image decoding, and JSON are all done with single-header vendored
  libraries (cpp-httplib, nlohmann/json, stb_image) — no Conan / pip / npm at
  runtime.

### Benchmarking

```bash
# 50 sequential requests with the same image
ab -n 50 -c 1 -p samples/face.jpg \
   -T 'multipart/form-data; boundary=---boundary' \
   http://localhost:8080/v1/validate

# 200 requests, 8 concurrent
ab -n 200 -c 8 -p samples/face.jpg \
   -T 'multipart/form-data; boundary=---boundary' \
   http://localhost:8080/v1/validate
```

Expect ~300 ms per request after a one-time ~3 s worker init at container
startup. With N workers, throughput should scale roughly linearly until you
saturate cores.

---

## Compatibility (per upstream BUILD.md)

| Component | Version |
|-----------|---------|
| OS | Ubuntu 22.04 (x86_64) |
| GCC | 11.4.0 |
| CMake | 3.28.3 (≥3.26 required) |
| Python | 3.10.12+ |
| Conan | 2.18.1 |
| OFIQ | v1.1.2 |

The API server adds:

| Component | Version |
|-----------|---------|
| C++ standard | C++17 |
| cpp-httplib | v0.18.5 (vendored) |
| nlohmann/json | v3.11.3 (vendored) |
| stb_image | pinned commit (vendored) |

OFIQ is **CPU-only** — no GPU/ROCm/CUDA needed.
