**fast-Serial-Codec** 是一个为 Windows 平台设计的高性能、现代化 C++ 异步串口通信库。它基于 Windows Overlapped I/O，通过多线程和回调机制，实现了高效的数据接收与灵活的数据处理（解码），是需要高速、稳定串口通信项目的理想选择。

## 作者与 AI 协作
本库由作者 **蓝色的风 (Lanse)** 与 AI 编程助手 **GitHub Copilot** 协作完成。
在整个开发过程中，AI 扮演了重要的辅助角色，包括但不限于：
- **代码优化**：针对关键代码（如线程管理、内存分配、异常捕获）提出改进建议，并引入 `std::unique_ptr` 等现代 C++ 特性以增强代码的健壮性和异常安全性。
- **环形队列模板化**：对原本存储`char`类型的环形队列，重构为环形队列模板。
- **潜在缺陷分析**：协助审查代码逻辑，发现并修复了在 `close` 后重新 `open` 等场景下可能存在的线程安全与资源管理问题。
- **文档生成**：辅助编写和润色了本文档（`README.md`），使其结构更清晰、内容更专业。
  
## 核心功能
- **高性能异步 I/O**：基于 Windows `OVERLAPPED` I/O 模型，实现非阻塞的数据收发，最大限度地减少 CPU 等待时间。
- **多线程数据接收**：独立的后台线程专门负责数据接收，确保主线程不会被 I/O 操作阻塞。
- **内置环形缓冲区**：自带线程安全的环形缓冲区（Ring Buffer），支持各种类型数据快速批量存储和读取，有效处理高速数据流，防止数据丢失。
- **灵活的数据处理/解码**：支持通过 `std::function` 注册自定义回调函数，让用户可以方便地实现自己的数据协议解析和解码逻辑，真正做到“即收即解”。
- **现代 C++ 设计**：采用 C++14 标准，利用 `std::thread`、`std::unique_ptr` 等特性，保证了代码的健壮性、异常安全性和易维护性。
- **简洁易用的 API**：提供清晰的 `open`, `close`, `send`, `readData` 等接口，易于集成和使用。
- **高度可配置**：支持自定义串口号、波特率、校验位、数据位、停止位、超时时间以及收发缓冲区大小。
## RingBuffer: 高性能环形缓冲区
`Fast-Serial-Codec` 的核心优势之一是其内置的 `RingBuffer` 类，它经过了多项优化以满足高并发和大数据流的需求：
- **模板化设计**：`RingBuffer<T>` 采用模板编程，使其不仅能用于 `char` 类型，还能灵活适应任何数据类型（如 `int`, `double` 或自定义结构体），具有极高的可复用性。
- **线程安全**：内部使用 `CRITICAL_SECTION` 对所有读写操作进行保护，确保在多线程环境下（例如，一个线程读、一个线程写）的数据一致性和安全性。
- **高性能内存拷贝**：`push` 和 `pop` 操作均使用 `std::copy` 进行批量内存复制。相比于循环逐个元素赋值，这种方式能更好地利用 CPU 指令，效率更高。
- **基于连续内存的数组**：底层数据结构采用动态数组，保证了内存的连续性。这带来了出色的缓存局部性（cache locality），进一步提升了数据存取速度。
- **优化的状态管理**：通过 `read_pos`、`write_pos` 和 `full` 标志位精确管理缓冲区状态，巧妙地解决了读写指针重合时“空”与“满”的判断难题，确保了逻辑的健壮性。

## 环境要求
- **操作系统**: Windows
- **编译器**: 支持 C++14 的编译器 (例如 Visual Studio 2017 或更高版本)
## 快速上手
将 `myserial.h` 和 `myserial.cpp` 添加到您的项目中即可开始使用。
### 示例 1: 基本收发,此示例演示了如何打开串口，发送数据，并从内置的环形缓冲区中读取返回的数据。
    void example_usage1()
    {
    	MySerial serial;
    	// 打开串口并自动启动读取线程
    	if (!serial.open("COM4", 256000))
    	{
    		printf("Failed to open serial port.\n");
    		return;
    	}
    	char data[8] = { 0xfe,0xff,0xff,0xfe,0xfe,0xff,0xff }; // 要发送的数据
    	char buff[1024]; // 接收缓冲区
    	serial.send(data, 7);// 发送一次数据到串口
    	Sleep(100); // 等待数据发送完成
    	while (1)
    	{
    		size_t bytesRead = serial.getSize(); // 获取环形缓冲区可读数据量
    		// 如果正好收到7字节，读取并处理
    		if (bytesRead == 7)
    		{
    			bytesRead = serial.readData(buff, 1024);
    			for (size_t i = 0; i < bytesRead; i++)
    				printf("%2X ", (uint8_t)buff[i]);
    			printf("\tBytes read: %zu\n", bytesRead);
    			serial.send(data, 7);// 继续发送数据
    		}
    		// 如果收到数据大于7字节，提示异常
    		if (bytesRead > 7)
    		{
    			printf("Error: Read more than 7 bytes, check your data handling logic.\n");
    			return;
    		}
    	}
    }
### 示例2：直接读取串口数据，绕过环形缓冲区
    void example_usage2()
    {
    	MySerial serial;
    	if (!serial.open("COM4", 256000))// 打开串口并自动启动读取线程
    	{
    		printf("Failed to open serial port.\n");
    		return;
    	}
    
    	char data[7] = { 0xfe,0xff,0xff,0xfe,0xfe,0xff,0xff }; // 要发送的数据
    	char buff[1024]; // 接收缓冲区
    	serial.Close_Buffer(); // 暂停读取线程，切换为直接读取模式
    	while (1)
    	{
    		serial.send(data, 7); // 发送数据到串口
    		// 直接从串口读取数据（不经过环形缓冲区）
    		int bytesRead = serial.receive(buff, 1024);
    		if (bytesRead > 0)
    		{
    			for (int i = 0; i < bytesRead; i++)
    				printf("%2X ", (uint8_t)buff[i]);
    			printf("\tBytes read: %d\n", bytesRead);
    		}
    		else if (bytesRead < 0)
    			printf("Error: receive failed.\n");
    		Sleep(100); // 可根据实际情况调整延时
    	}
    }
### 示例3：使用`RingBuffer<int64_t> userBuffer`,和`serial.setDataProcessFunc`，在接收串口数据的同时，自动将8字节数据拼接在一起，存入用户环形队列中。
    void example_usage3()
    {
    	MySerial serial;
    	RingBuffer<int64_t> userBuffer;
    	userBuffer.Init(40960 / 8); // 初始化用户缓冲区，假设每个数据8字节
    	// 打开串口并自动启动读取线程
    	if (!serial.open("COM4", 256000))
    	{
    		printf("Failed to open serial port.\n");
    		return;
    	}
    	char data[8] = { 0xfe,0xff,0xff,0xfe,0xfe,0xff,0xff }; // 要发送的数据
    	//连续8个字节为一个数据，按找小端对齐方式存入用户自定义环形队列
    	serial.setDataProcessFunc([&userBuffer](const char* src, size_t src_size)
    		{
    			// 直接存入用户自定义队列
    			//先判断 src_size 是否为8的倍数，如果不是，先存起来到下次
    			static RingBuffer<char> tempBuffer;
    			if (!tempBuffer.isFull())
    				tempBuffer.push(src, src_size);
    			size_t availableSize = tempBuffer.getSize();
    			size_t toProcess = availableSize - ( availableSize % 8 ); // 计算可处理的字节数，确保是8的倍数
    			if (toProcess >= 8)
    			{
    				char temp[8];
    				while (toProcess >= 8)
    				{
    					tempBuffer.pop(temp, 8);//小端模式，低字节在前，高字节在后
    					int64_t value = *reinterpret_cast<int64_t*>( temp );
    					userBuffer.push(&value, 1);
    					toProcess -= 8;
    				}
    			}
    			// 也可在此做数据过滤、解析等
    		});
    
    	while (1)
    	{
    		serial.send(data, 8);// 发送一次数据到串口
    		// 获取用户缓冲区可读数据量
    		size_t dataSize = userBuffer.getSize();
    		if (dataSize >= 1)// 如果正好收到1个数据，读取并处理
    		{
    			int64_t value;
    			userBuffer.pop(&value, 1);
    			printf("Received data: %lld\n", value);
    		}
    		Sleep(10);
    	}
    }
## API 参考
### 主要方法
 - `bool open(const char* portName, int baudRate, ...)`: 打开并配置串口。
- `void close()`: 关闭串口，释放所有资源。
- `bool isOpen() const`: 检查串口是否已打开。
- `int send(const char* buffer, int size)`: 异步发送数据。
- `size_t readData(char* buffer, size_t size)`: 从内部环形缓冲区读取数据。
- `size_t getSize() const`: 获取环形缓冲区中可读数据的字节数。
- `bool hasData() const`: 检查环形缓冲区中是否有数据。
### 高级配置
- `void setDataProcessFunc(const DataProcessFunc& func)`: 设置自定义数据处理回调函数。`DataProcessFunc` 的类型为 `std::function<void(const char* src, size_t src_size)>`。
- `bool setBufferSize(int size)`: 设置串口驱动程序的内部缓冲区大小。
- `void setReadTimeout(DWORD timeout)`: 设置读取操作的超时时间（毫秒）。
- `void setWriteTimeout(DWORD timeout)`: 设置写入操作的超时时间（毫秒）。

## 欢迎提交 Pull Request 或创建 Issue 来改进项目！
