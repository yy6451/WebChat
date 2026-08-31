# WebChat AI Coding Guidelines

## Architecture Overview
This is a C++ lightweight web server for Linux, implementing a high-performance concurrent model using:
- Thread pool + non-blocking sockets + epoll (ET/LT modes) + event handling (Reactor/Proactor patterns)
- State machine-based HTTP request parsing (GET/POST)
- Database integration for user registration/login
- Synchronous/asynchronous logging system
- Timer-based inactive connection handling

## Key Components
- **WebServer**: Main orchestrator class managing epoll, thread pool, database pool, and timers
- **Connection Hierarchy**: Base `Connection` class with `HttpConnection` and `WebSocketConnection` subclasses
- **Protocol Layer**: `HttpRequest`/`HttpResponse` for HTTP parsing/generation
- **Utilities**: Thread pool, SQL connection pool (RAII pattern), sorted timer list, async logging with block queue

## Build System
- **Primary Build**: `make server` (links with `-lpthread -lmysqlclient`)
- **Alternative**: CMake (requires Threads and MySQL packages)
- **Dependencies**: MySQL development libraries, pthreads
- **Output**: `server` executable in project root

## Configuration
Server behavior controlled via command-line arguments parsed by `Config` class:
- Port, log write mode, trigger modes (ET/LT combinations), linger options
- Database pool size, thread pool size, log enable/disable, actor model (Reactor/Proactor)

## Coding Patterns
- **Thread Safety**: Use `locker` class for mutexes, `sem` for semaphores
- **Resource Management**: RAII pattern extensively used (e.g., `connectionRAII`)
- **Logging**: Singleton `Log` class with macros (`LOG_ERROR`, `LOG_INFO`, etc.)
- **Error Handling**: Asserts and error logging, no exceptions in core paths
- **File I/O**: Memory mapping for static file serving (`mmap`/`munmap`)

## Development Workflow
- **Testing**: Use `webbench` in `test_pressure/` for load testing (rebuild if needed)
- **Debugging**: Enable debug mode in makefile (`DEBUG=1`) for symbols
- **Database Setup**: Requires MySQL with `qgydb` database and `user` table
- **Web Interface**: Static HTML files in `root/` directory served by server

## Common Tasks
- **Adding Endpoints**: Extend `HttpConnection::process()` with new URL handlers
- **Database Operations**: Use `connectionRAII` to get/release connections from pool
- **WebSocket Support**: Implement in `WebSocketConnection` using `WebSocketCodec`
- **Logging**: Use appropriate LOG_ macros, check `close_log` config

## File Structure Conventions
- Headers in same directory as implementations (e.g., `HttpConnection.h`/`HttpConnection.cpp`)
- Utils organized by functionality (`log/`, `sql/`, `timer/`, etc.)
- Protocol-specific code in `protocol/` subdirectories
- Module extensions in `module/` (e.g., chat functionality)

## Performance Considerations
- Epoll modes (ET/LT) configurable for different workloads
- Thread pool size and database pool size tunable via config
- Asynchronous logging to avoid blocking I/O operations
- Timer wheel for efficient inactive connection cleanup