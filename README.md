# blobfuse-simulation

This project demonstrates the use of shared memory in a C++ application, utilizing Docker for containerization and Kubernetes for deployment. The application includes both writer and reader modes to interact with shared memory.

## Files

- **Dockerfile**: Defines the Docker images for building and running the application.
- **Makefile**: Provides commands to build and push Docker images.
- **deploy/**: Contains Kubernetes deployment and storage configuration files.
- **shared_memory_example.cpp**: Main application file with writer and reader modes.
- **shared_memory_example_detail.cpp**: Contains detailed implementation of shared memory operations.
- **shared_memory_example_sample.cpp**: Sample code for using direct I/O.
- **shared_memory_example_shm.cpp**: Example of using POSIX shared memory.

## Building Docker Images

To build the Docker images for both writer and reader modes, use the `Makefile`:

```sh
make build
```

To build only the writer image:

```sh
make build-writer
```

To build only the reader image:

```sh
make build-reader
```

## Pushing Docker Images

To push the Docker images to a registry, use the `Makefile`:

```sh
make push
```

To push only the writer image:

```sh
make push-writer
```

To push only the reader image:

```sh
make push-reader
```

## Kubernetes Deployment

The `deploy/` directory contains Kubernetes deployment files:

- **pvc-blobfuse.yaml.me**: PersistentVolumeClaim for blobfuse storage (ME environment).
- **pvc-blobfuse.yaml.pg**: PersistentVolumeClaim for blobfuse storage (PG environment).
- **shared_memory_example_deploy.yaml.me**: Deployment configuration for the ME environment.
- **shared_memory_example_deploy.yaml.pg**: Deployment configuration for the PG environment.
- **storageclass_cache_blobfuse.yaml.me**: StorageClass configuration for blobfuse (ME environment).
- **storageclass_cache_blobfuse.yaml.pg**: StorageClass configuration for blobfuse (PG environment).

## Running the Application

To run the application in writer mode:

```sh
docker run --rm -it shared_memory_example_writer
```

To run the application in reader mode:

```sh
docker run --rm -it shared_memory_example_reader
```

## License

This project is licensed under the MIT License.
```

This 

README.md

 provides an overview of the project, instructions for building and pushing Docker images, and details on Kubernetes deployment.