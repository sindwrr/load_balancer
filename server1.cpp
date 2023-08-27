// Сервер #1
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#define PORT 1200 // порт сервера
#define MAXLINE 128 // размер буфера

int main() 
{
	int sockfd;
	char buffer[MAXLINE];
	struct sockaddr_in servaddr, cliaddr;
	
	// создаем сокет для взаимодействия с балансировщиком
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) 
	{
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}
	
	// очищаем адреса сервера и клиента
	memset(&servaddr, 0, sizeof(servaddr));
	memset(&cliaddr, 0, sizeof(cliaddr));
	
	// заполняем информацию о сервере
	servaddr.sin_family = AF_INET; // протокол (IPv4)
	servaddr.sin_addr.s_addr = INADDR_ANY; // связка с локальными интерфейсами
	servaddr.sin_port = htons(PORT); // порт сервера
	
	// связываем сокет с адресом сервера
	if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
	{
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
	
	// получаем датаграмму от балансировщика
	socklen_t len = sizeof(cliaddr);
    while (true)
    {
	    recvfrom(sockfd, (char *)buffer, MAXLINE,
				MSG_WAITALL, ( struct sockaddr *) &cliaddr,
				&len);
	    std::cout << "Client: " << buffer << std::endl;
    }
	
	return 0;
}
