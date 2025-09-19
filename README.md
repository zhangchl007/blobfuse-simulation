# blobfuse-simulation

A minimal demonstration of large (GB‑scale) file mapping and hot swapping of model files on a shared (blobfuse mounted) volume using Boost.Interprocess in C++, packaged with Docker and deployed via Kubernetes (AKS friendly).  
Two roles:
- Writer: periodically (re)generates a large frozen model-like file and atomically updates a manifest.
- Reader: watches the manifest, mmaps the pointed file with `boost::interprocess::file_mapping` + `mapped_region`, pre-faults pages, validates header, and logs metadata.

## Source Overview

Key runtime structures in [shared_memory_example.cpp](shared_memory_example.cpp):
- Header struct `FrozenHeader`
- Entry/model structs
- Loader class [`FrozenHashMapImpl`](shared_memory_example.cpp) with page prefetch (madvise + touch)
- Manifest change detection loop: `ManifestWatchLoop`
- File generation: `GenerateBigModelFile`

## Build (Local)

### Prerequisites
Ubuntu / Debian (or container), g++, Boost (filesystem/system/interprocess), make, Docker (optional).

### Compile only
```sh
g++ -O2 -std=c++17 -o shared_memory_example shared_memory_example.cpp -lboost_system -lrt -lpthread
```

### Run (Writer)
```sh
./shared_memory_example writer-loop
```

### Run (Reader – manifest optional)
```sh
./shared_memory_example watch /mnt/blobfuse/frozen_kv.manifest 5
# or rely on MODEL_BASE env:
MODEL_BASE=/mnt/blobfuse/frozen_kv ./shared_memory_example watch
```

## Environment Variables

| Variable | Writer | Reader | Default | Meaning |
|----------|--------|--------|---------|---------|
| MODEL_BASE | ✓ | ✓ | /mnt/blobfuse/frozen_kv | Base path prefix for generated versions (writer) and manifest derivation (reader). |
| FILE_SIZE_BYTES | ✓ | | 2147483648 | Size per generated file (2 GiB default). |
| VERSION_COUNT | ✓ | | 5 | Number of versions per cycle. |
| VERSION_UPDATE_INTERVAL_SEC | ✓ | | 5 | Seconds between version generations. |
| CYCLES | ✓ | | 0 | 0 = infinite loop of version sets. |
| WATCH_INTERVAL_SEC | | ✓ | 5 | Polling interval for manifest / file mtime. |
| TERM | | ✓ | (unset) | Optional to silence ncurses issues in minimal base images. |

## Docker

Build (single image reused for writer & reader):
```sh
docker build -t shared_memory_example:local .
```

Example run:
```sh
# Writer
docker run --rm -v /host/blobfuse:/mnt/blobfuse shared_memory_example:local ./shared_memory_example writer-loop
# Reader
docker run --rm -v /host/blobfuse:/mnt/blobfuse shared_memory_example:local ./shared_memory_example watch
```

If you maintain separate tags (Makefile approach):
```sh
make build-writer TAG=v1.2
make build-reader TAG=v1.2
make push-writer TAG=v1.2
make push-reader TAG=v1.2
```

## Kubernetes (AKS Friendly)

1. Apply storage & PVC (adjust for your cluster/class):
```sh
kubectl apply -f deploy/storageclass_cache_blobfuse.yaml.me
kubectl apply -f deploy/pvc-blobfuse.yaml.me
```

2. Deploy:
```sh
kubectl apply -f deploy/shared_memory_example_deploy.yaml.me
```

3. Verify:
```sh
kubectl get pods
kubectl logs -f deploy/shared-memory-example-writer
kubectl logs -f deploy/shared-memory-example-reader
```

### Common Deployment Pitfall

If reader pods show `watch` utility help text and CrashLoopBackOff:
- The container executed the Linux `watch` command instead of the binary.
- Fix by specifying `command: ["/app/shared_memory_example"]` in the container spec (already present in the current YAML) OR use `ENTRYPOINT` in Dockerfile and pass args only.

### Rolling Update (new image tag)
```sh
kubectl set image deployment/shared-memory-example-writer shared-memory-example-writer=zhangchl007/shared_memory_example_writer:v1.3
kubectl set image deployment/shared-memory-example-reader shared-memory-example-reader=zhangchl007/shared_memory_example_reader:v1.3
```

## Runtime Flow (Reader)

1. Derive manifest path.
2. Stat loop detects mtime change of manifest, reads target filename.
3. On new target: `file_mapping` + `mapped_region`.
4. Validate header (`magic == "STRATEGY"`, version).
5. Compute pointers to model array, bucket list, entries, value pool.
6. Page prefetch:
   - `madvise(MADV_WILLNEED[, MADV_POPULATE_READ])`
   - Manual stride touch (`TouchPages`)
7. Log success & metadata.

## Extending

- Add metrics (Prometheus) around build latency & page faults.
- Introduce checksum in header to validate integrity.
- Add graceful stop via signal handler updating an atomic flag.
- Support delta diff or partial re-map if large value pool stable.

## Troubleshooting

| Symptom | Cause | Action |
|---------|-------|--------|
| CrashLoopBackOff with `watch` help | Wrong entrypoint | Set explicit `command`. |
| Slow first access | Pages not resident | Keep prefetch + touch; consider staggering loads. |
| File too small error | Writer incomplete | Ensure writer finished; check PVC performance. |
| Manifest flickers | Partial writes | Use existing atomic write (temp + rename) – already implemented in `AtomicWriteFile`. |

## Security / Performance Notes

- Large file creation uses `posix_fallocate` to avoid sparse extents.
- Memory mapping is read-only in reader (`bip::read_only`).
- Consider non-root images for production hardening (current final stage runs as non-root user).

## License

MIT License (include LICENSE file if distributing).

---
Generated components reference:  
- Loader: [`FrozenHashMapImpl::Build`](shared_memory_example.cpp)  
- Model generator: (`GenerateBigModelFile` in [shared_memory_example.cpp](shared_memory_example.cpp))  
- Watch loop: (`ManifestWatchLoop` in [shared_memory_example.cpp](shared_memory_example.cpp))