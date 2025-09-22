#include <windows.h>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <algorithm>
#include<string>
#include<thread>
/**
 * @brief ͨ�û��λ�����ģ����
 * @tparam T �������洢����������
 * ֧�ֶ��̰߳�ȫ���ʺϸ���������������
 * @author ��ɫ�ķ� (Lanse)
 */
template<typename T>
class RingBuffer
{
private:
	T* buffer = NULL;               // ���ݴ洢����
	size_t capacity = 0;            // ������������
	volatile size_t read_pos = 0;   // ��ָ��λ��
	volatile size_t write_pos = 0;  // дָ��λ��
	volatile bool full = 0;         // ����������־
	CRITICAL_SECTION cs;        // �ٽ�����������ͬ��

public:
	/**
	 * @brief ���캯������ʼ����Ա����
	 */
	RingBuffer()
	{
		InitializeCriticalSection(&cs);
	}

	/**
	 * @brief �����������ͷ���Դ
	 */
	~RingBuffer()
	{
		if (buffer)
			delete[] buffer;
		DeleteCriticalSection(&cs);
	}

	/**
	 * @brief ��ʼ��������
	 * @param bufsize ��������С��Ԫ�ظ�����
	 * @return �Ƿ��ʼ���ɹ�
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
	 * @brief д�����ݵ�������
	 * @param data ��д�����������
	 * @param data_size д�������Ԫ�ظ���
	 * @return �Ƿ�д��ɹ����ռ䲻�㷵�� false ��
	 */
	bool push(const T data[], const size_t data_size)
	{
		if (data == nullptr && data_size > 0) return false;
		if (data_size == 0) return true;
		if (!buffer) return false;

		EnterCriticalSection(&cs);

		// ������ÿռ�
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
			return false; // �ռ䲻��
		}

		// д�����ݣ����ܷ����Σ�
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

		// ��������־
		if (write_pos == read_pos)
			full = true;

		LeaveCriticalSection(&cs);
		return true;
	}


	/**
	 * @brief �ӻ�������ȡ����
	 * @param out_buffer ��ȡ�������ݴ������
	 * @param size ������ȡ������Ԫ�ظ���
	 * @return ʵ�ʶ�ȡ������Ԫ�ظ���
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

		// ����ɶ�������
		size_t available;
		if (full)
			available = capacity;
		else if (write_pos > read_pos)
			available = write_pos - read_pos;
		else
			available = capacity - read_pos + write_pos;

		// ʵ�ʶ�ȡ��������������
		size_t items_to_read = ( size < available ) ? size : available;

		// ��ȡ���ݣ����ܷ����Σ�
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

		// �����дָ���ص�������Ϊ��ʼλ��
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
	 * @brief ��黺�����Ƿ�Ϊ��
	 * @return �Ƿ�Ϊ��
	 */
	bool isEmpty() const
	{
		return ( !full && ( read_pos == write_pos ) );
	}

	/**
	 * @brief ��黺�����Ƿ�����
	 * @return �Ƿ�����
	 */
	bool isFull() const
	{
		return full;
	}

	/**
	 * @brief ��ȡ��ǰ�������Ѵ�������
	 * @return ����Ԫ�ظ���
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
//�����첽ģʽ������ͬ��ģʽ��
class MySerial
{
	HANDLE hCom = NULL;// ���ھ��
	OVERLAPPED overRead = {};  // �������� OVERLAPPED �ṹ��
	OVERLAPPED overWrite = {}; // д������ OVERLAPPED �ṹ��
	DWORD readTimeout = 1;  // ��ȡ��ʱʱ��(����)
	DWORD writeTimeout = 1; // д�볬ʱʱ��(����)

	int buffsize = 10240; // ���ڻ�������С
	DWORD ERROR_code = 0;// ������

	RingBuffer<char> dataBuffer;     // ���λ�����
	const int maxBufferSize = 40960; // ��󻺳�����С

	volatile bool ReadThreadProc_puse = false; // ���ƶ�ȡ�̵߳�����
	volatile bool ReadThreadProc_Control_stop = false; // ���ƶ�ȡ�̵߳�����״̬
	volatile bool ReadThreadProc_Control_status = false; // ���ƶ�ȡ�̵߳�����״̬

	// �������������ݴ�����
	using DataProcessFunc = std::function<void(const char* src, size_t src_size)>;
	DataProcessFunc dataProcessFunc = nullptr;

public:
	MySerial()
	{};
	~MySerial()
	{
		close();
	}

	// �򿪴�������
	bool open(const char* portName, const int baudRate, const char parity = 0, const char databit = 8, const char stopbit = 1);
	// �رմ�������
	void close();
	// ��鴮���Ƿ��
	bool isOpen() const;
	// �첽��������
	int send(const char* buffer, int size_max);
	// ֱ�Ӷ�ȡ���ݣ����������λ�������
	int receive(char* data, size_t size_max);

	// ���û�������С�ķ���
	bool setBufferSize(const int size);
	// ���ö���ʱ�ķ���
	void setReadTimeout(DWORD timeout)
	{
		readTimeout = timeout;
	}
	// ����д�볬ʱʱ��
	void setWriteTimeout(DWORD timeout)
	{
		writeTimeout = timeout;
	}

	// ���λ�������ط���
private:
	// ��ʼ�����ݻ�����
	bool initDataBuffer(const int size = 40960);
public:
	// �ӻ�������ȡ���ݻ���ֱͨ,����ReadThreadProc_puse����
	size_t readData(char* buffer, const size_t size);
	// �Ƿ������ݿɶ�
	bool hasData() const;
	// ��ȡ�ɶ�������
	size_t getSize() const;
	//ʹ�û��λ�����
	void Open_Buffer()
	{
		ReadThreadProc_puse = true;
	}
	//��ʹ�û��λ�����
	void Close_Buffer()
	{
		ReadThreadProc_puse = false;
	}

	//*******************************��ȡ�̺߳���********************************************
private:
	void InitReadThreadFlag()
	{
		ReadThreadProc_puse = false;
		ReadThreadProc_Control_stop = false;
		ReadThreadProc_Control_status = false;
	}
	// ��ȡ�̺߳���
	void ReadThreadProc();
	// ��ȡ�߳�״̬
	bool getThreadStatus() const
	{
		return ReadThreadProc_Control_status;
	}
	/**
	* @brief ֹͣ��ȡ�̲߳��ȴ��䰲ȫ�˳�
	*/
	void StopReadThread()
	{
		ReadThreadProc_Control_stop = true;
		while (getThreadStatus())
		{
			Sleep(10); // �ȴ��߳̽���
		}
	}
public:
	/**
	 * @brief ���ƶ�ȡ�̵߳���������ͣ
	 * @param ju true=��ͣ��ֱ�Ӵ��ڶ�����false=�ָ������λ���������
	 */
	void Purse_ReadThread(bool ju)
	{
		ReadThreadProc_puse = ju;
	}

	//�����Զ������ݴ�����
	void setDataProcessFunc(const DataProcessFunc& func)
	{
		dataProcessFunc = func;
	}


};





