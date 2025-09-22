#include <cstdio>
#include "myserial.h"

/**
 * @brief 串口通信主程序示例
 *        发送数据到串口，并循环读取返回数据，支持环形缓冲区模式
 * 假设串口设备会返回发送的数据
 */

 // 示例1：使用环形缓冲区读取数据
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

	// 发送一次数据到串口
	serial.send(data, 7);
	Sleep(100); // 等待数据发送完成

	while (1)
	{
		// 获取环形缓冲区可读数据量
		size_t bytesRead = serial.getSize();

		// 如果正好收到7字节，读取并处理
		if (bytesRead == 7)
		{
			bytesRead = serial.readData(buff, 1024);
			for (size_t i = 0; i < bytesRead; i++)
			{
				printf("%2X ", (uint8_t)buff[i]);
			}
			printf("\tBytes read: %zu\n", bytesRead);

			// 继续发送数据
			serial.send(data, 7);
		}

		// 如果收到数据大于7字节，提示异常
		if (bytesRead > 7)
		{
			printf("Error: Read more than 7 bytes, check your data handling logic.\n");
			return;
		}

		// 延时，避免CPU占用过高
		Sleep(1);
	}
}
// 示例2：直接读取串口数据，绕过环形缓冲区
void example_usage2()
{
	MySerial serial;


	// 打开串口并自动启动读取线程
	if (!serial.open("COM4", 256000))
	{
		printf("Failed to open serial port.\n");
		return;
	}

	char data[7] = { 0xfe,0xff,0xff,0xfe,0xfe,0xff,0xff }; // 要发送的数据
	char buff[1024]; // 接收缓冲区

	// 暂停读取线程，切换为直接读取模式
	serial.Close_Buffer();


	while (1)
	{
		// 发送数据到串口
		serial.send(data, 7);

		// 直接从串口读取数据（不经过环形缓冲区）
		int bytesRead = serial.receive(buff, 1024);
		if (bytesRead > 0)
		{
			for (int i = 0; i < bytesRead; i++)
			{
				printf("%2X ", (uint8_t)buff[i]);
			}
			printf("\tBytes read: %d\n", bytesRead);
		}
		else if (bytesRead < 0)
		{
			printf("Error: receive failed.\n");
		}

		Sleep(100); // 可根据实际情况调整延时
	}

}
//示例3：使用用户自定义环形缓冲区读取数据
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
			{
				tempBuffer.push(src, src_size);
			}
			size_t availableSize = tempBuffer.getSize();
			size_t toProcess = availableSize - ( availableSize % 8 ); // 计算可处理的字节数，确保是8的倍数
			if (toProcess >= 8)
			{
				char temp[8];
				while (toProcess >= 8)
				{
					//小端模式，低字节在前，高字节在后
					tempBuffer.pop(temp, 8);
					int64_t value = *reinterpret_cast<int64_t*>( temp );
					userBuffer.push(&value, 1);
					toProcess -= 8;
				}
			}
			// 也可在此做数据过滤、解析等
		});

	while (1)
	{
		// 发送一次数据到串口
		serial.send(data, 8);
		// 获取用户缓冲区可读数据量
		size_t dataSize = userBuffer.getSize();
		// 如果正好收到1个数据，读取并处理
		if (dataSize >= 1)
		{
			int64_t value;
			userBuffer.pop(&value, 1);
			printf("Received data: %lld\n", value);
		}
		Sleep(10);
	}
}
int main()
{
	example_usage1();
	return 0;
}

