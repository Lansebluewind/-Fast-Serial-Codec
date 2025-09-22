#include "myserial.h"
#include <system_error>
#include <iostream>


bool MySerial::open(const char* portName, const int baudRate, const char parity, const char databit, const char stopbit)
{
	// 构建串口名称 - 确保使用Windows需要的格式
	std::string portStr(portName);
	if (portStr.substr(0, 3) != "COM")
		return false;
	std::string szPort = "\\\\.\\" + portStr;

	hCom = CreateFileA(szPort.c_str(), //串口名
		GENERIC_READ | GENERIC_WRITE, //支持读写
		0, //独占方式，串口不支持共享
		NULL,//安全属性指针，默认值为NULL
		OPEN_EXISTING, //打开现有的串口文件
		FILE_FLAG_OVERLAPPED,       // 关键：启用异步I/O
		NULL);//用于复制文件句柄，默认值为NULL，对串口而言该参数必须置为NULL

	// 如果打开失败，释放资源并返回
	if (hCom == INVALID_HANDLE_VALUE)
	{
		ERROR_code = GetLastError();
		return false;
	}

	//配置缓冲区大小 
	if (!SetupComm(hCom, buffsize, buffsize))
	{
		CloseHandle(hCom);
		ERROR_code = GetLastError();
		return false;
	}

	// 配置参数 
	DCB p;
	memset(&p, 0, sizeof(p));
	p.DCBlength = sizeof(p);
	p.BaudRate = baudRate; // 波特率
	p.ByteSize = databit; // 数据位

	//校验位
	switch (parity)
	{
	case 0:
		p.Parity = NOPARITY; //无校验
		break;
	case 1:
		p.Parity = ODDPARITY; //奇校验
		break;
	case 2:
		p.Parity = EVENPARITY; //偶校验
		break;
	case 3:
		p.Parity = MARKPARITY; //标记校验
		break;
	default:
		p.Parity = NOPARITY;
		break;
	}
	//停止位
	switch (stopbit)
	{
	case 1:
		p.StopBits = ONESTOPBIT; //1位停止位
		break;
	case 2:
		p.StopBits = TWOSTOPBITS; //2位停止位
		break;
	case 3:
		p.StopBits = ONE5STOPBITS; //1.5位停止位
		break;
	default:
		p.StopBits = ONESTOPBIT;
		break;
	}

	// 设置流控制参数
	p.fOutxCtsFlow = FALSE;                  // 禁用CTS流控
	p.fOutxDsrFlow = FALSE;                  // 禁用DSR流控
	p.fDtrControl = DTR_CONTROL_ENABLE;      // 启用DTR线
	p.fDsrSensitivity = FALSE;               // 不敏感DSR
	p.fRtsControl = RTS_CONTROL_ENABLE;      // 启用RTS线
	p.fOutX = FALSE;                         // 禁用XON/XOFF流控
	p.fInX = FALSE;                          // 禁用XON/XOFF流控
	p.fErrorChar = FALSE;                    // 禁用错误替换
	p.fBinary = TRUE;                        // 二进制模式
	p.fNull = FALSE;                         // 不丢弃NULL字节
	p.fAbortOnError = FALSE;                 // 发生错误时不中止

	// 设置参数失败
	if (!SetCommState(hCom, &p))
	{

		ERROR_code = GetLastError();
		CloseHandle(hCom);
		return false;
	}

	// 设置超时参数，配置为异步模式最佳的设置
	COMMTIMEOUTS timeouts;
	timeouts.ReadIntervalTimeout = MAXDWORD;  // 任意两字符间的最大时延
	timeouts.ReadTotalTimeoutMultiplier = 0;  // 读取每字符的时延乘数
	timeouts.ReadTotalTimeoutConstant = 0;    // 读取操作的固定时延
	timeouts.WriteTotalTimeoutMultiplier = 0; // 写入每字符的时延乘数
	timeouts.WriteTotalTimeoutConstant = 0;   // 写入操作的固定时延
	// 设置参数失败
	if (!SetCommTimeouts(hCom, &timeouts))
	{
		ERROR_code = GetLastError();
		CloseHandle(hCom);
		return false;
	}

	// 清除可能存在的错误和缓冲区
	PurgeComm(hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	// 初始化 overlapped 结构体，用于异步操作
	memset(&overRead, 0, sizeof(OVERLAPPED));
	memset(&overWrite, 0, sizeof(OVERLAPPED));

	// 创建事件句柄 ,然后启动读取线程
	overRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	overWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (overRead.hEvent == NULL || overWrite.hEvent == NULL)
	{
		if (overRead.hEvent != NULL) CloseHandle(overRead.hEvent);
		if (overWrite.hEvent != NULL) CloseHandle(overWrite.hEvent);
		CloseHandle(hCom);
		ERROR_code = GetLastError();
		std::cerr << "CreateEvent failed." << std::endl;
		return false;
	}

	// 初始化环形缓冲区
	initDataBuffer(maxBufferSize);

	try
	{
		// 启动读取线程
		InitReadThreadFlag();
		std::thread([this]()
			{
				ReadThreadProc();
			}).detach();

	}
	catch (const std::system_error& e)
	{
		if (overRead.hEvent != NULL) CloseHandle(overRead.hEvent);
		if (overWrite.hEvent != NULL) CloseHandle(overWrite.hEvent);
		CloseHandle(hCom);
		ERROR_code = e.code().value();
		std::cerr << "Read Thread creation failed: " << e.what() << std::endl;
		return false;
	}
	return true;
}

bool MySerial::setBufferSize(const int size)
{
	// 保存新的缓冲区大小设置
	buffsize = size;

	// 如果串口未打开，只记录新的大小设置，下次 open 时使用
	if (!isOpen())
		return true;  // 成功，但没有实际更改

	// 串口已打开，需要重新设置缓冲区

	// 1. 取消所有挂起的 I/O 操作
	CancelIo(hCom);

	// 2. 清空缓冲区
	PurgeComm(hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	// 3. 应用新的缓冲区大小
	if (!SetupComm(hCom, size, size))
	{
		ERROR_code = GetLastError();
		return false;  // 设置失败
	}

	// 4. 重置 OVERLAPPED 结构的事件，确保之前的事件状态不会影响新的操作
	if (overRead.hEvent != NULL)
	{
		ResetEvent(overRead.hEvent);
	}

	if (overWrite.hEvent != NULL)
	{
		ResetEvent(overWrite.hEvent);
	}

	return true;  // 返回设置的新缓冲区大小
}

// 关闭串口连接并释放资源
void MySerial::close()
{
	// 停止读取线程
	StopReadThread();

	// 关闭前确保端口已打开
	if (hCom != NULL && hCom != INVALID_HANDLE_VALUE)
	{
		// 取消所有挂起的I/O操作
		CancelIo(hCom);

		// 清空缓冲区
		PurgeComm(hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

		// 关闭串口句柄
		CloseHandle(hCom);
		hCom = NULL;
	}

	// 关闭事件句柄
	if (overRead.hEvent != NULL)
	{
		CloseHandle(overRead.hEvent);
		overRead.hEvent = NULL;
	}

	if (overWrite.hEvent != NULL)
	{
		CloseHandle(overWrite.hEvent);
		overWrite.hEvent = NULL;
	}
}

// 检查串口是否打开
bool MySerial::isOpen() const
{
	return ( hCom != NULL && hCom != INVALID_HANDLE_VALUE );
}

// 异步发送数据
int MySerial::send(const char* buffer, int size_max)
{
	ERROR_code = 0;
	//参数检查
	if (buffer == NULL)
		return -1; // 错误：数据缓冲区不能为空
	if (size_max <= 0 || size_max > buffsize)
		return -1;// 错误：缓冲区大小必须大于0且不超过设置的缓冲区大小

	if (!isOpen())
		return -1;

	DWORD bytesWritten = 0;
	DWORD dwError = 0;
	BOOL fRes;

	// 重置事件状态
	ResetEvent(overWrite.hEvent);

	// 开始异步写操作
	fRes = WriteFile(hCom, buffer, size_max, &bytesWritten, &overWrite);

	// 检查操作是否立即完成
	if (!fRes)
	{
		dwError = GetLastError();
		if (dwError != ERROR_IO_PENDING)
		{
			// 写入出错
			ERROR_code = GetLastError();
			return -1;
		}

		// 操作挂起，等待完成
		DWORD dwWait = WaitForSingleObject(overWrite.hEvent, writeTimeout); // 等待

		if (dwWait == WAIT_OBJECT_0)
		{
			// 获取操作结果
			if (!GetOverlappedResult(hCom, &overWrite, &bytesWritten, FALSE))
			{
				ERROR_code = GetLastError();
				return -1;
			}
		}
		else if (dwWait == WAIT_TIMEOUT)
		{
			// 等待超时，但操作仍在后台进行
			return 0;
		}
		else
		{
			// 等待出错
			CancelIo(hCom);
			ERROR_code = GetLastError();
			return -1;
		}
	}
	return bytesWritten;
}

// 异步接收数据
// 异步接收数据 - 支持灵活的超时策略
int MySerial::receive(char* data, size_t size_max)
{
	ERROR_code = 0;
	//参数检查
	if (data == NULL)
		return -1; // 错误：数据缓冲区不能为空

	//判断 size_maxs 不能超过DWORD的最大值
	if (size_max > 0xFFFFFFFF)
		return -1; // 错误：size_max不能超过DWORD的最大值

	if (size_max <= 0 || size_max > buffsize)
		return -1;// 错误：缓冲区大小必须大于0且不超过设置的缓冲区大小

	if (!isOpen())
		return -1;

	DWORD bytesRead = 0;
	DWORD dwError = 0;
	COMSTAT comStat;

	// 获取当前错误状态和串口状态
	if (!ClearCommError(hCom, &dwError, &comStat))
	{
		ERROR_code = GetLastError();
		return -1;  // 获取状态失败
	}

	// 检查是否有数据可读
	if (comStat.cbInQue == 0)
	{
		// 没有数据且不等待 (readTimeout = 0)
		if (readTimeout == 0)
			return 0;

		// 没有数据但需要等待
	}


	// 调整读取大小，不超过实际可读字节数和缓冲区大小

	DWORD bytesToRead = ( comStat.cbInQue > 0 && ( size_max > (int)comStat.cbInQue ) ) ?
		comStat.cbInQue : (DWORD)size_max;

	// 重置事件状态
	ResetEvent(overRead.hEvent);

	// 开始异步读操作
	BOOL fRes = ReadFile(hCom, data, bytesToRead, &bytesRead, &overRead);

	// 如果立即完成
	if (fRes)
	{
		return bytesRead;
	}

	// 检查操作是否挂起
	dwError = GetLastError();
	if (dwError != ERROR_IO_PENDING)
	{
		// 读取出错
		ERROR_code = GetLastError();
		return -1;
	}

	// 等待读取完成或超时
	DWORD dwWait = WaitForSingleObject(overRead.hEvent, readTimeout);

	if (dwWait == WAIT_OBJECT_0)
	{
		// 获取操作结果
		if (!GetOverlappedResult(hCom, &overRead, &bytesRead, FALSE))
		{
			// 操作失败
			ERROR_code = GetLastError();
			return -1;
		}
		return bytesRead;
	}
	else if (dwWait == WAIT_TIMEOUT)
	{
		// 超时，没有读取到数据，取消操作
		CancelIo(hCom);
		return 0;
	}
	else
	{
		// 等待出错
		CancelIo(hCom);
		ERROR_code = GetLastError();
		return -1;
	}
}

// 读取线程函数实现
void MySerial::ReadThreadProc()
{
	ReadThreadProc_Control_status = true;
	int size_ss = this->buffsize;
	auto temp_buffer = std::make_unique<char[]>(size_ss);//临时缓冲区

	while (!ReadThreadProc_Control_stop)
	{
		if (ReadThreadProc_puse)
		{
			Sleep(100);
			continue;
		}
		if (size_ss != this->buffsize)//如果缓冲区大小改变，重新分配
		{
			size_ss = this->buffsize;
			temp_buffer = std::make_unique<char[]>(size_ss);//重新分配
		}
		int bytesRead = receive(temp_buffer.get(), size_ss);

		if (bytesRead > 0)
		{
			if (dataProcessFunc)
			{
				// 用户自定义处理，用户可在此函数内存入自己的环形队列
				dataProcessFunc(temp_buffer.get(), bytesRead);
			}
			else
			{
				// 默认存入内部队列
				dataBuffer.push(temp_buffer.get(), bytesRead);
			}

		}
	}
	ReadThreadProc_Control_status = false;
}

// 初始化数据缓冲区
bool MySerial::initDataBuffer(const int size)
{
	return dataBuffer.Init(size);
}

// 从缓冲区读取数据
size_t MySerial::readData(char* buffer, const size_t size)
{
	// 根据 ReadThreadProc_puse 自动切换数据来源：
	// true：直接串口读取；false：从环形缓冲区读取
	if (ReadThreadProc_puse)//如果读取线程被暂停，直接读取
		return receive(buffer, size);
	else
		return dataBuffer.pop(buffer, size);
}

// 检查是否有数据可读
bool MySerial::hasData() const
{
	return !dataBuffer.isEmpty();
}

// 获取可读数据量
size_t MySerial::getSize() const
{
	return dataBuffer.getSize();
}



