# Makefile for building Docker images

# Image names
IMAGE_NAME_WRITER = zhangchl007/shared_memory_example_writer
IMAGE_NAME_READER = zhangchl007/shared_memory_example_reader

# Dockerfile path
DOCKERFILE = Dockerfile

# Default tag
TAG ?= latest

# Build the writer image
build-writer:
	sed -i 's/MODE/writer/g' $(DOCKERFILE)
	docker build --build-arg MODE=writer -t $(IMAGE_NAME_WRITER):$(TAG) -f $(DOCKERFILE) .
	@echo "Writer image built with tag: $(TAG)"

# Build the reader image
build-reader:
	sed -i 's/MODE/reader/g' $(DOCKERFILE)
	docker build --build-arg MODE=reader -t $(IMAGE_NAME_READER):$(TAG) -f $(DOCKERFILE) .
	@echo "Reader image built with tag: $(TAG)"

# Build both images
build: build-writer build-reader

# Push the writer image to a registry
push-writer:
	docker push $(IMAGE_NAME_WRITER):$(TAG)

# Push the reader image to a registry
push-reader:
	docker push $(IMAGE_NAME_READER):$(TAG)

# Push both images
push: push-writer push-reader

# Clean up local images
clean:
	docker rmi $(IMAGE_NAME_WRITER):$(TAG) $(IMAGE_NAME_READER):$(TAG)

.PHONY: build-writer build-reader build push-writer push-reader push clean