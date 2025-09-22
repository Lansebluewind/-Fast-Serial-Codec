#include "myserial.h"
#include <system_error>
#include <iostream>


bool MySerial::open(const char* portName, const int baudRate, const char parity, const char databit, const char stopbit)
{
	// ������������ - ȷ��ʹ��Windows��Ҫ�ĸ�ʽ
	std::string portStr(portName);
	if (portStr.substr(0, 3) != "COM")
		return false;
	std::string szPort = "\\\\.\\" + portStr;

	hCom = CreateFileA(szPort.c_str(), //������
		GENERIC_READ | GENERIC_WRITE, //֧�ֶ�д
		0, //��ռ��ʽ�����ڲ�֧�ֹ���
		NULL,//��ȫ����ָ�룬Ĭ��ֵΪNULL
		OPEN_EXISTING, //�����еĴ����ļ�
		FILE_FLAG_OVERLAPPED,       // �ؼ��������첽I/O
		NULL);//���ڸ����ļ������Ĭ��ֵΪNULL���Դ��ڶ��Ըò���������ΪNULL

	// �����ʧ�ܣ��ͷ���Դ������
	if (hCom == INVALID_HANDLE_VALUE)
	{
		ERROR_code = GetLastError();
		return false;
	}

	//���û�������С 
	if (!SetupComm(hCom, buffsize, buffsize))
	{
		CloseHandle(hCom);
		ERROR_code = GetLastError();
		return false;
	}

	// ���ò��� 
	DCB p;
	memset(&p, 0, sizeof(p));
	p.DCBlength = sizeof(p);
	p.BaudRate = baudRate; // ������
	p.ByteSize = databit; // ����λ

	//У��λ
	switch (parity)
	{
	case 0:
		p.Parity = NOPARITY; //��У��
		break;
	case 1:
		p.Parity = ODDPARITY; //��У��
		break;
	case 2:
		p.Parity = EVENPARITY; //żУ��
		break;
	case 3:
		p.Parity = MARKPARITY; //���У��
		break;
	default:
		p.Parity = NOPARITY;
		break;
	}
	//ֹͣλ
	switch (stopbit)
	{
	case 1:
		p.StopBits = ONESTOPBIT; //1λֹͣλ
		break;
	case 2:
		p.StopBits = TWOSTOPBITS; //2λֹͣλ
		break;
	case 3:
		p.StopBits = ONE5STOPBITS; //1.5λֹͣλ
		break;
	default:
		p.StopBits = ONESTOPBIT;
		break;
	}

	// ���������Ʋ���
	p.fOutxCtsFlow = FALSE;                  // ����CTS����
	p.fOutxDsrFlow = FALSE;                  // ����DSR����
	p.fDtrControl = DTR_CONTROL_ENABLE;      // ����DTR��
	p.fDsrSensitivity = FALSE;               // ������DSR
	p.fRtsControl = RTS_CONTROL_ENABLE;      // ����RTS��
	p.fOutX = FALSE;                         // ����XON/XOFF����
	p.fInX = FALSE;                          // ����XON/XOFF����
	p.fErrorChar = FALSE;                    // ���ô����滻
	p.fBinary = TRUE;                        // ������ģʽ
	p.fNull = FALSE;                         // ������NULL�ֽ�
	p.fAbortOnError = FALSE;                 // ��������ʱ����ֹ

	// ���ò���ʧ��
	if (!SetCommState(hCom, &p))
	{

		ERROR_code = GetLastError();
		CloseHandle(hCom);
		return false;
	}

	// ���ó�ʱ����������Ϊ�첽ģʽ��ѵ�����
	COMMTIMEOUTS timeouts;
	timeouts.ReadIntervalTimeout = MAXDWORD;  // �������ַ�������ʱ��
	timeouts.ReadTotalTimeoutMultiplier = 0;  // ��ȡÿ�ַ���ʱ�ӳ���
	timeouts.ReadTotalTimeoutConstant = 0;    // ��ȡ�����Ĺ̶�ʱ��
	timeouts.WriteTotalTimeoutMultiplier = 0; // д��ÿ�ַ���ʱ�ӳ���
	timeouts.WriteTotalTimeoutConstant = 0;   // д������Ĺ̶�ʱ��
	// ���ò���ʧ��
	if (!SetCommTimeouts(hCom, &timeouts))
	{
		ERROR_code = GetLastError();
		CloseHandle(hCom);
		return false;
	}

	// ������ܴ��ڵĴ���ͻ�����
	PurgeComm(hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	// ��ʼ�� overlapped �ṹ�壬�����첽����
	memset(&overRead, 0, sizeof(OVERLAPPED));
	memset(&overWrite, 0, sizeof(OVERLAPPED));

	// �����¼���� ,Ȼ��������ȡ�߳�
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

	// ��ʼ�����λ�����
	initDataBuffer(maxBufferSize);

	try
	{
		// ������ȡ�߳�
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
	// �����µĻ�������С����
	buffsize = size;

	// �������δ�򿪣�ֻ��¼�µĴ�С���ã��´� open ʱʹ��
	if (!isOpen())
		return true;  // �ɹ�����û��ʵ�ʸ���

	// �����Ѵ򿪣���Ҫ�������û�����

	// 1. ȡ�����й���� I/O ����
	CancelIo(hCom);

	// 2. ��ջ�����
	PurgeComm(hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

	// 3. Ӧ���µĻ�������С
	if (!SetupComm(hCom, size, size))
	{
		ERROR_code = GetLastError();
		return false;  // ����ʧ��
	}

	// 4. ���� OVERLAPPED �ṹ���¼���ȷ��֮ǰ���¼�״̬����Ӱ���µĲ���
	if (overRead.hEvent != NULL)
	{
		ResetEvent(overRead.hEvent);
	}

	if (overWrite.hEvent != NULL)
	{
		ResetEvent(overWrite.hEvent);
	}

	return true;  // �������õ��»�������С
}

// �رմ������Ӳ��ͷ���Դ
void MySerial::close()
{
	// ֹͣ��ȡ�߳�
	StopReadThread();

	// �ر�ǰȷ���˿��Ѵ�
	if (hCom != NULL && hCom != INVALID_HANDLE_VALUE)
	{
		// ȡ�����й����I/O����
		CancelIo(hCom);

		// ��ջ�����
		PurgeComm(hCom, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);

		// �رմ��ھ��
		CloseHandle(hCom);
		hCom = NULL;
	}

	// �ر��¼����
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

// ��鴮���Ƿ��
bool MySerial::isOpen() const
{
	return ( hCom != NULL && hCom != INVALID_HANDLE_VALUE );
}

// �첽��������
int MySerial::send(const char* buffer, int size_max)
{
	ERROR_code = 0;
	//�������
	if (buffer == NULL)
		return -1; // �������ݻ���������Ϊ��
	if (size_max <= 0 || size_max > buffsize)
		return -1;// ���󣺻�������С�������0�Ҳ��������õĻ�������С

	if (!isOpen())
		return -1;

	DWORD bytesWritten = 0;
	DWORD dwError = 0;
	BOOL fRes;

	// �����¼�״̬
	ResetEvent(overWrite.hEvent);

	// ��ʼ�첽д����
	fRes = WriteFile(hCom, buffer, size_max, &bytesWritten, &overWrite);

	// �������Ƿ��������
	if (!fRes)
	{
		dwError = GetLastError();
		if (dwError != ERROR_IO_PENDING)
		{
			// д�����
			ERROR_code = GetLastError();
			return -1;
		}

		// �������𣬵ȴ����
		DWORD dwWait = WaitForSingleObject(overWrite.hEvent, writeTimeout); // �ȴ�

		if (dwWait == WAIT_OBJECT_0)
		{
			// ��ȡ�������
			if (!GetOverlappedResult(hCom, &overWrite, &bytesWritten, FALSE))
			{
				ERROR_code = GetLastError();
				return -1;
			}
		}
		else if (dwWait == WAIT_TIMEOUT)
		{
			// �ȴ���ʱ�����������ں�̨����
			return 0;
		}
		else
		{
			// �ȴ�����
			CancelIo(hCom);
			ERROR_code = GetLastError();
			return -1;
		}
	}
	return bytesWritten;
}

// �첽��������
// �첽�������� - ֧�����ĳ�ʱ����
int MySerial::receive(char* data, size_t size_max)
{
	ERROR_code = 0;
	//�������
	if (data == NULL)
		return -1; // �������ݻ���������Ϊ��

	//�ж� size_maxs ���ܳ���DWORD�����ֵ
	if (size_max > 0xFFFFFFFF)
		return -1; // ����size_max���ܳ���DWORD�����ֵ

	if (size_max <= 0 || size_max > buffsize)
		return -1;// ���󣺻�������С�������0�Ҳ��������õĻ�������С

	if (!isOpen())
		return -1;

	DWORD bytesRead = 0;
	DWORD dwError = 0;
	COMSTAT comStat;

	// ��ȡ��ǰ����״̬�ʹ���״̬
	if (!ClearCommError(hCom, &dwError, &comStat))
	{
		ERROR_code = GetLastError();
		return -1;  // ��ȡ״̬ʧ��
	}

	// ����Ƿ������ݿɶ�
	if (comStat.cbInQue == 0)
	{
		// û�������Ҳ��ȴ� (readTimeout = 0)
		if (readTimeout == 0)
			return 0;

		// û�����ݵ���Ҫ�ȴ�
	}


	// ������ȡ��С��������ʵ�ʿɶ��ֽ����ͻ�������С

	DWORD bytesToRead = ( comStat.cbInQue > 0 && ( size_max > (int)comStat.cbInQue ) ) ?
		comStat.cbInQue : (DWORD)size_max;

	// �����¼�״̬
	ResetEvent(overRead.hEvent);

	// ��ʼ�첽������
	BOOL fRes = ReadFile(hCom, data, bytesToRead, &bytesRead, &overRead);

	// ����������
	if (fRes)
	{
		return bytesRead;
	}

	// �������Ƿ����
	dwError = GetLastError();
	if (dwError != ERROR_IO_PENDING)
	{
		// ��ȡ����
		ERROR_code = GetLastError();
		return -1;
	}

	// �ȴ���ȡ��ɻ�ʱ
	DWORD dwWait = WaitForSingleObject(overRead.hEvent, readTimeout);

	if (dwWait == WAIT_OBJECT_0)
	{
		// ��ȡ�������
		if (!GetOverlappedResult(hCom, &overRead, &bytesRead, FALSE))
		{
			// ����ʧ��
			ERROR_code = GetLastError();
			return -1;
		}
		return bytesRead;
	}
	else if (dwWait == WAIT_TIMEOUT)
	{
		// ��ʱ��û�ж�ȡ�����ݣ�ȡ������
		CancelIo(hCom);
		return 0;
	}
	else
	{
		// �ȴ�����
		CancelIo(hCom);
		ERROR_code = GetLastError();
		return -1;
	}
}

// ��ȡ�̺߳���ʵ��
void MySerial::ReadThreadProc()
{
	ReadThreadProc_Control_status = true;
	int size_ss = this->buffsize;
	auto temp_buffer = std::make_unique<char[]>(size_ss);//��ʱ������

	while (!ReadThreadProc_Control_stop)
	{
		if (ReadThreadProc_puse)
		{
			Sleep(100);
			continue;
		}
		if (size_ss != this->buffsize)//�����������С�ı䣬���·���
		{
			size_ss = this->buffsize;
			temp_buffer = std::make_unique<char[]>(size_ss);//���·���
		}
		int bytesRead = receive(temp_buffer.get(), size_ss);

		if (bytesRead > 0)
		{
			if (dataProcessFunc)
			{
				// �û��Զ��崦���û����ڴ˺����ڴ����Լ��Ļ��ζ���
				dataProcessFunc(temp_buffer.get(), bytesRead);
			}
			else
			{
				// Ĭ�ϴ����ڲ�����
				dataBuffer.push(temp_buffer.get(), bytesRead);
			}

		}
	}
	ReadThreadProc_Control_status = false;
}

// ��ʼ�����ݻ�����
bool MySerial::initDataBuffer(const int size)
{
	return dataBuffer.Init(size);
}

// �ӻ�������ȡ����
size_t MySerial::readData(char* buffer, const size_t size)
{
	// ���� ReadThreadProc_puse �Զ��л�������Դ��
	// true��ֱ�Ӵ��ڶ�ȡ��false���ӻ��λ�������ȡ
	if (ReadThreadProc_puse)//�����ȡ�̱߳���ͣ��ֱ�Ӷ�ȡ
		return receive(buffer, size);
	else
		return dataBuffer.pop(buffer, size);
}

// ����Ƿ������ݿɶ�
bool MySerial::hasData() const
{
	return !dataBuffer.isEmpty();
}

// ��ȡ�ɶ�������
size_t MySerial::getSize() const
{
	return dataBuffer.getSize();
}



