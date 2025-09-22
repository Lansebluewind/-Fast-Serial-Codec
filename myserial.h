#include <windows.h>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <algorithm>
#include<string>
#include<thread>
/**
 * @brief 通用环形缓冲区模板类
 * @tparam T 缓冲区存储的数据类型
 * 支持多线程安全，适合高性能数据流场景
 * @author 蓝色的风 (Lanse)
 */
template<typename T>
class RingBuffer
{
private:
	T* buffer = NULL;               // 数据存储数组
	size_t capacity = 0;            // 缓冲区总容量
	volatile size_t read_pos = 0;   // 读指针位置
	volatile size_t write_pos = 0;  // 写指针位置
	volatile bool full = 0;         // 缓冲区满标志
	CRITICAL_SECTION cs;        // 临界区对象用于同步

public:
	/**
	 * @brief 构造函数，初始化成员变量
	 */
	RingBuffer()
	{
		InitializeCriticalSection(&cs);
	}

	/**
	 * @brief 析构函数，释放资源
	 */
	~RingBuffer()
	{
		if (buffer)
			delete[] buffer;
		DeleteCriticalSection(&cs);
	}

	/**
	 * @brief 初始化缓冲区
	 * @param bufsize 缓冲区大小（元素个数）
	 * @return 是否初始化成功
	 */
	bool Init(const int bufsize)
	{
		EnterCriticalSection(&cs);
		if (buffer)
			delete[] buffer;
		try
		{
			buffer = new T[bufsize];
			capacity = bufsize;
			read_pos = 0;
			write_pos = 0;
			full = false;
			LeaveCriticalSection(&cs);
			return true;
		}
		catch (...)
		{
			LeaveCriticalSection(&cs);
			return false;
		}
	}

	/**
	 * @brief 写入数据到缓冲区
	 * @param data 待写入的数据数组
	 * @param data_size 写入的数据元素个数
	 * @return 是否写入成功（空间不足返回 false ）
	 */
	bool push(const T data[], const size_t data_size)
	{
		if (data == nullptr && data_size > 0) return false;
		if (data_size == 0) return true;
		if (!buffer) return false;

		EnterCriticalSection(&cs);

		// 计算可用空间
		size_t available;
		if (full)
			available = 0;
		else if (write_pos >= read_pos)
			available = capacity - write_pos + read_pos;
		else
			available = read_pos - write_pos;

		if (available <= data_size)
		{
			LeaveCriticalSection(&cs);
			return false; // 空间不足
		}

		// 写入数据（可能分两段）
		if (write_pos + data_size <= capacity)
		{
			std::copy(data, data + data_size, buffer + write_pos);
			write_pos = ( write_pos + data_size ) % capacity;
		}
		else
		{
			size_t first_chunk = capacity - write_pos;
			std::copy(data, data + first_chunk, buffer + write_pos);
			std::copy(data + first_chunk, data + data_size, buffer);
			write_pos = data_size - first_chunk;
		}

		// 更新满标志
		if (write_pos == read_pos)
			full = true;

		LeaveCriticalSection(&cs);
		return true;
	}


	/**
	 * @brief 从缓冲区读取数据
	 * @param out_buffer 读取到的数据存放数组
	 * @param size 期望读取的数据元素个数
	 * @return 实际读取的数据元素个数
	 */
	size_t pop(T* out_buffer, const size_t size)
	{
		if (out_buffer == nullptr && size > 0) return false;
		if (!buffer) return false;

		EnterCriticalSection(&cs);

		if (isEmpty())
		{
			LeaveCriticalSection(&cs);
			return 0;
		}

		// 计算可读数据量
		size_t available;
		if (full)
			available = capacity;
		else if (write_pos > read_pos)
			available = write_pos - read_pos;
		else
			available = capacity - read_pos + write_pos;

		// 实际读取量不超过请求量
		size_t items_to_read = ( size < available ) ? size : available;

		// 读取数据（可能分两段）
		if (read_pos + items_to_read <= capacity)
		{
			std::copy(buffer + read_pos, buffer + read_pos + items_to_read, out_buffer);
			read_pos = ( read_pos + items_to_read ) % capacity;
		}
		else
		{
			size_t first_chunk = capacity - read_pos;
			std::copy(buffer + read_pos, buffer + read_pos + first_chunk, out_buffer);
			std::copy(buffer, buffer + ( items_to_read - first_chunk ), out_buffer + first_chunk);
			read_pos = items_to_read - first_chunk;
		}

		// 如果读写指针重叠，重置为起始位置
		if (read_pos == write_pos)
		{
			read_pos = 0;
			write_pos = 0;
			full = false;
		}
		else
		{
			full = false;
		}

		LeaveCriticalSection(&cs);
		return items_to_read;
	}

	/**
	 * @brief 检查缓冲区是否为空
	 * @return 是否为空
	 */
	bool isEmpty() const
	{
		return ( !full && ( read_pos == write_pos ) );
	}

	/**
	 * @brief 检查缓冲区是否已满
	 * @return 是否已满
	 */
	bool isFull() const
	{
		return full;
	}

	/**
	 * @brief 获取当前缓冲区已存数据量
	 * @return 数据元素个数
	 */
	size_t getSize() const
	{
		if (full)
			return capacity;
		else if (write_pos >= read_pos)
			return write_pos - read_pos;
		else
			return capacity - read_pos + write_pos;
	}
};
//采用异步模式，不用同步模式。
class MySerial
{
	HANDLE hCom = NULL;// 串口句柄
	OVERLAPPED overRead = {};  // 读操作的 OVERLAPPED 结构体
	OVERLAPPED overWrite = {}; // 写操作的 OVERLAPPED 结构体
	DWORD readTimeout = 1;  // 读取超时时间(毫秒)
	DWORD writeTimeout = 1; // 写入超时时间(毫秒)

	int buffsize = 10240; // 串口缓冲区大小
	DWORD ERROR_code = 0;// 错误码

	RingBuffer<char> dataBuffer;     // 环形缓冲区
	const int maxBufferSize = 40960; // 最大缓冲区大小

	volatile bool ReadThreadProc_puse = false; // 控制读取线程的运行
	volatile bool ReadThreadProc_Control_stop = false; // 控制读取线程的运行状态
	volatile bool ReadThreadProc_Control_status = false; // 控制读取线程的运行状态

	// 新增：串口数据处理函数
	using DataProcessFunc = std::function<void(const char* src, size_t src_size)>;
	DataProcessFunc dataProcessFunc = nullptr;

public:
	MySerial()
	{};
	~MySerial()
	{
		close();
	}

	// 打开串口连接
	bool open(const char* portName, const int baudRate, const char parity = 0, const char databit = 8, const char stopbit = 1);
	// 关闭串口连接
	void close();
	// 检查串口是否打开
	bool isOpen() const;
	// 异步发送数据
	int send(const char* buffer, int size_max);
	// 直接读取数据（不经过环形缓冲区）
	int receive(char* data, size_t size_max);

	// 设置缓冲区大小的方法
	bool setBufferSize(const int size);
	// 设置读超时的方法
	void setReadTimeout(DWORD timeout)
	{
		readTimeout = timeout;
	}
	// 设置写入超时时间
	void setWriteTimeout(DWORD timeout)
	{
		writeTimeout = timeout;
	}

	// 环形缓冲区相关方法
private:
	// 初始化数据缓冲区
	bool initDataBuffer(const int size = 40960);
public:
	// 从缓冲区读取数据或者直通,根据ReadThreadProc_puse而定
	size_t readData(char* buffer, const size_t size);
	// 是否有数据可读
	bool hasData() const;
	// 获取可读数据量
	size_t getSize() const;
	//使用环形缓冲区
	void Open_Buffer()
	{
		ReadThreadProc_puse = true;
	}
	//不使用环形缓冲区
	void Close_Buffer()
	{
		ReadThreadProc_puse = false;
	}

	//*******************************读取线程函数********************************************
private:
	void InitReadThreadFlag()
	{
		ReadThreadProc_puse = false;
		ReadThreadProc_Control_stop = false;
		ReadThreadProc_Control_status = false;
	}
	// 读取线程函数
	void ReadThreadProc();
	// 获取线程状态
	bool getThreadStatus() const
	{
		return ReadThreadProc_Control_status;
	}
	/**
	* @brief 停止读取线程并等待其安全退出
	*/
	void StopReadThread()
	{
		ReadThreadProc_Control_stop = true;
		while (getThreadStatus())
		{
			Sleep(10); // 等待线程结束
		}
	}
public:
	/**
	 * @brief 控制读取线程的运行与暂停
	 * @param ju true=暂停（直接串口读），false=恢复（环形缓冲区读）
	 */
	void Purse_ReadThread(bool ju)
	{
		ReadThreadProc_puse = ju;
	}

	//设置自定义数据处理函数
	void setDataProcessFunc(const DataProcessFunc& func)
	{
		dataProcessFunc = func;
	}


};





