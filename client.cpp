// Клиент
#include <fstream>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>

#define MAXLINE 128 // макс. длина отправляемого сообщения
#define SLEEP 50000 // время (в мс) между отправкой двух соседних датаграмм

int main() 
{
	int sockfd;
	char query[MAXLINE];
	struct sockaddr_in servaddr;

	std::ifstream file;
    std::string port;

	// читаем информацию о порте балансировщика
	// из конфигурационного файла
    file.open("config.conf");
    if (file.is_open())
    {
        getline(file, port);
        file.close();
    }
    else
        std::cout << "Unable to open config file." << std::endl;

	// создаем сокет для взаимодействия с балансировщиком
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
    {
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	// очищаем адрес балансировщика
	memset(&servaddr, 0, sizeof(servaddr));
	
	// заполняем информацию о сервере
	servaddr.sin_family = AF_INET; // протокол (IPv4)
	servaddr.sin_addr.s_addr = INADDR_ANY; // связка с локальными интерфейсами
	servaddr.sin_port = htons(stoi(port)); // порт сервера
	
	// посылаем датаграммы вида "Query i",
	// где i - натуральное число
    char i_str[5 + sizeof(char)];
    int i = 0;
	while (true)
	{
		// формируем строку для отправки
		sprintf(i_str, "%d", i);
        strcpy(query, "Query ");
        strcat(query, i_str);

		// отправляем строку
		sendto(sockfd, (const char *) query, strlen(query),
		0, (const struct sockaddr *) &servaddr,
			sizeof(servaddr));
		std::cout << query << " sent."<<std::endl;
		++i;

		// ждем до отправления след. датаграммы
		usleep(SLEEP);
	}

	// по завершении закрываем сокет
	close(sockfd);
	return 0;
}
