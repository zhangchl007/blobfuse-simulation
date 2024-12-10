# Stage 1: Build stage
FROM ubuntu:20.04 AS builder

# Set environment variable to avoid tzdata prompts
ENV DEBIAN_FRONTEND=noninteractive

# Install dependencies required for building the application
RUN apt-get update && \
    apt-get install -y g++ make libboost-all-dev && \
    rm -rf /var/lib/apt/lists/*

# Set up working directory and copy source code
WORKDIR /app
COPY shared_memory_example.cpp .

# Compile the C++ code with enhanced logging
RUN g++ -o shared_memory_example shared_memory_example.cpp -lboost_system -lrt -lpthread && \
    echo "Compilation successful" || echo "Compilation failed"

# Stage 2: Minimal runtime image
FROM ubuntu:20.04

# Create a new user and group
RUN groupadd -r appuser && useradd -r -g appuser appuser

# Copy only the compiled binary from the builder stage
COPY --from=builder /app/shared_memory_example /app/shared_memory_example

# Set the working directory
WORKDIR /app

# Change ownership of the application directory to the new user
RUN chown -R appuser:appuser /app

# Switch to the new user
USER appuser

# Entry point for the container
CMD ["./shared_memory_example", "writer"]