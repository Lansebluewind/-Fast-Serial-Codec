#include <cstdio>
#include "myserial.h"

/**
 * @brief ����ͨ��������ʾ��
 *        �������ݵ����ڣ���ѭ����ȡ�������ݣ�֧�ֻ��λ�����ģʽ
 * ���贮���豸�᷵�ط��͵�����
 */

 // ʾ��1��ʹ�û��λ�������ȡ����
void example_usage1()
{
	MySerial serial;
	// �򿪴��ڲ��Զ�������ȡ�߳�
	if (!serial.open("COM4", 256000))
	{
		printf("Failed to open serial port.\n");
		return;
	}

	char data[8] = { 0xfe,0xff,0xff,0xfe,0xfe,0xff,0xff }; // Ҫ���͵�����
	char buff[1024]; // ���ջ�����

	// ����һ�����ݵ�����
	serial.send(data, 7);
	Sleep(100); // �ȴ����ݷ������

	while (1)
	{
		// ��ȡ���λ������ɶ�������
		size_t bytesRead = serial.getSize();

		// ��������յ�7�ֽڣ���ȡ������
		if (bytesRead == 7)
		{
			bytesRead = serial.readData(buff, 1024);
			for (size_t i = 0; i < bytesRead; i++)
			{
				printf("%2X ", (uint8_t)buff[i]);
			}
			printf("\tBytes read: %zu\n", bytesRead);

			// ������������
			serial.send(data, 7);
		}

		// ����յ����ݴ���7�ֽڣ���ʾ�쳣
		if (bytesRead > 7)
		{
			printf("Error: Read more than 7 bytes, check your data handling logic.\n");
			return;
		}

		// ��ʱ������CPUռ�ù���
		Sleep(1);
	}
}
// ʾ��2��ֱ�Ӷ�ȡ�������ݣ��ƹ����λ�����
void example_usage2()
{
	MySerial serial;


	// �򿪴��ڲ��Զ�������ȡ�߳�
	if (!serial.open("COM4", 256000))
	{
		printf("Failed to open serial port.\n");
		return;
	}

	char data[7] = { 0xfe,0xff,0xff,0xfe,0xfe,0xff,0xff }; // Ҫ���͵�����
	char buff[1024]; // ���ջ�����

	// ��ͣ��ȡ�̣߳��л�Ϊֱ�Ӷ�ȡģʽ
	serial.Close_Buffer();


	while (1)
	{
		// �������ݵ�����
		serial.send(data, 7);

		// ֱ�ӴӴ��ڶ�ȡ���ݣ����������λ�������
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

		Sleep(100); // �ɸ���ʵ�����������ʱ
	}

}
//ʾ��3��ʹ���û��Զ��廷�λ�������ȡ����
void example_usage3()
{
	MySerial serial;
	RingBuffer<int64_t> userBuffer;
	userBuffer.Init(40960 / 8); // ��ʼ���û�������������ÿ������8�ֽ�
	// �򿪴��ڲ��Զ�������ȡ�߳�
	if (!serial.open("COM4", 256000))
	{
		printf("Failed to open serial port.\n");
		return;
	}
	char data[8] = { 0xfe,0xff,0xff,0xfe,0xfe,0xff,0xff }; // Ҫ���͵�����
	//����8���ֽ�Ϊһ�����ݣ�����С�˶��뷽ʽ�����û��Զ��廷�ζ���
	serial.setDataProcessFunc([&userBuffer](const char* src, size_t src_size)
		{
			// ֱ�Ӵ����û��Զ������
			//���ж� src_size �Ƿ�Ϊ8�ı�����������ǣ��ȴ��������´�
			static RingBuffer<char> tempBuffer;
			if (!tempBuffer.isFull())
			{
				tempBuffer.push(src, src_size);
			}
			size_t availableSize = tempBuffer.getSize();
			size_t toProcess = availableSize - ( availableSize % 8 ); // ����ɴ�����ֽ�����ȷ����8�ı���
			if (toProcess >= 8)
			{
				char temp[8];
				while (toProcess >= 8)
				{
					//С��ģʽ�����ֽ���ǰ�����ֽ��ں�
					tempBuffer.pop(temp, 8);
					int64_t value = *reinterpret_cast<int64_t*>( temp );
					userBuffer.push(&value, 1);
					toProcess -= 8;
				}
			}
			// Ҳ���ڴ������ݹ��ˡ�������
		});

	while (1)
	{
		// ����һ�����ݵ�����
		serial.send(data, 8);
		// ��ȡ�û��������ɶ�������
		size_t dataSize = userBuffer.getSize();
		// ��������յ�1�����ݣ���ȡ������
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

