#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>

#define SIZE 128
#define SIZE_LOGIN 32

char CREDENTIALS[SIZE]; /* Aqui vai constar o caminho das credências de acesso para todos os
                         * utilizadores. */
int SIGNAL = -1;        /* Inicialização da variável de controlo de sucesso ou insucesso aquando
                         * a recepção de um sinal. */

/* Limpa o ecrã. */
void clearEcran()
{
	if(fork() == 0) 
	{
		execlp("clear", "clear", NULL);
	}
	else 
	{
		wait(NULL);
	}
}

/* Lê uma linha de um ficheiro a partir de um fd. */
ssize_t readln(int fd, char *buffer, size_t nbyte)
{
	int i;

	for(i = 0; read(fd, buffer + i, 1) == 1 && buffer[i] != '\n' && i < (int)nbyte; i++);
	buffer[i] = '\0';
	return i;
}

/* O nome de utilizador e a palavra passe só estão correctos caso não apresentem espaços. */
int isInputOk(char *str, int n_byte)
{
	int i, r;
	for(r = 1, i = 0; str[i] != '\0' && i < n_byte; i++)
	{
		if(str[i] == ' ')
		{
			r = 0;			
		}
	}
	return r;
}

/* Verifica se um utilizador já está previamente registado. */
int existsUser(char *user)
{
	int fd, i, r;
	char c;
	fd = open(CREDENTIALS, O_RDONLY);
	i = r = 0;
	while(read(fd, &c, 1) > 0 && r == 0)
	{	
		if(c == ' ' && user[i] == '\n') /* Se chegar a um espaço e a string do utilizador a verificar
		                                 * chegou ao fim então obtivemos sucesso. Existe. */
		{		
			r = 1;
		}
		else if(user[i] != c)
		{
			i = 0;
			while(read(fd, &c, 1) > 0 && c != '\n');
		}
		else	
		{ 
			i++;					
		}
	}
	close(fd);
	return r;		
}

/* Devolve o caminho de onde o cliente está a correr. */
char *getPath()
{
	int fd;
	char *path;

	path = malloc(SIZE * sizeof(char));
	fd = open(".temp/.path", O_CREAT | O_TRUNC | O_WRONLY, 0666);

	if(fork() == 0)
	{
		dup2(fd, 1);
		close(fd);

		execlp("pwd", "pwd", NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		close(fd);
	}

	fd = open(".temp/.path", O_RDONLY);
	read(fd, path, SIZE); /* Lemos o output de pwd que consta em .temp e devolvemos. */
	close(fd);

	return strtok(path, "\n");
}

/* Função handler de sinais de sucesso. */
void sucess(int s)
{
	SIGNAL = 0;
}

void non_sucess(int s)
{
	SIGNAL = 1;
}

void access_server(char *client_name)
{
	int fd, fd_find, fd_error, counter, p[2];
	char *str, *cmds, *line, *path, *buffer, *files, *command, *msg;
	
	line = malloc(SIZE *  sizeof(char));
	path = malloc(SIZE *  sizeof(char));

	getlogin_r(line, SIZE);
	sprintf(path, "/home/%s/.Backup/eve", line); /* Directoria do fifo para se comunicar com o servidor. */

	fd = open(path, O_WRONLY);
	
	if(fd == -1)
	{
		str = "Não foi possível comunicar com o servidor. A encerrar...\n"; /* Fifo não foi criado ou não existe. */
		write(1, str, strlen(str));
		free(line);
		free(path);
		exit(-1);
	}

	files = malloc(SIZE * sizeof(char));
	cmds = malloc(SIZE * sizeof(char));
	buffer = malloc(SIZE * sizeof(char));
	command = malloc(SIZE * sizeof(char));
	msg = malloc(SIZE * sizeof(char));

	path = getPath();

	str = "Insira um comando:\n> ";
	write(1, str, strlen(str));

	fd_error = open(".temp/stderr", O_CREAT | O_APPEND | O_WRONLY, 0666);

	/* Enquanto o utilizador não inserir 0, continuar-lhe-à sendo pedido comandos. */
	while(read(0, cmds, SIZE) > 0 && cmds[0] != '0')
	{
		/* O utilizador pode inserir uma string com vários argumentos, isto é,
		 * vários espaços entre comandos ou ficheiros. Torna-se mais seguro
		 * utilizar strncmp. */  
		if(strncmp(cmds, "backup", 6) == 0)
		{
			cmds = strtok(cmds, " ");
			files = strtok(NULL, "\n");

			/* Verificar se é folder. Caso seja, procederemos com um método de backup para pastas. */
			
			pipe(p);

			if(fork() == 0)
			{
				dup2(fd_error, 2);
				close(fd_error);

				dup2(p[1], 1);
				close(p[1]);
				close(p[0]);

				/* Este comando devolve 1 caso seja seja de facto um folder, ou 0 em caso contrário. */
				sprintf(command, "test -d %s && echo '1' || echo '0'", files);		
				
				execl("/bin/sh", "sh", "-c", command, NULL);
		
				_exit(1);
			}
			else
			{
				wait(NULL);
				close(p[1]);
				
				read(p[0], command, SIZE); /* Lemos o resultado do teste anterior (test -d). */
				close(p[0]);
			}

			if(*command == '0') /* Não é uma pasta. */
			{
				fd_find = open(".temp/.find", O_CREAT | O_TRUNC | O_WRONLY, 0666);
				if(fork() == 0)
				{
					dup2(fd_error, 2);
					close(fd_error);
					
					dup2(fd_find, 1);
					close(fd_find);

					sprintf(command, "ls -1 %s", files);
					execl("/bin/sh", "sh", "-c", command, NULL);
					_exit(1);
				}
				else
				{
					wait(NULL);
					close(fd_find);
				}

				fd_find = open(".temp/.find", O_RDONLY);
				
				counter = 0;
				while(readln(fd_find, buffer, SIZE) > 0)
				{
					counter++; /* Controla se existem ficheiros com o nome dado por parte do utilizador. */
					sprintf(line, "%s %s %s %s %d", client_name, cmds, buffer, path, getpid());
					
					write(fd, line, strlen(line) + 1);
				
					signal(SIGUSR1, sucess);
					signal(SIGUSR2, non_sucess);

					pause();
					
					if(SIGNAL == 0)
					{
						sprintf(msg, "%s: copiado\n", buffer);
						write(1, msg, strlen(msg) + 1);
					}
					else if(SIGNAL == 1)
					{
						sprintf(msg, "%s: não copiado\n", buffer);
						write(1, msg, strlen(msg) + 1);
					}
					SIGNAL = -1;
				}

				if(counter == 0) /* Se não tiver entrado no ciclo então o(s) ficheiro(s) indicado(s) não existia(m). */
				{
					sprintf(msg, "%s: não existe\n", files);
					write(1, msg, strlen(msg) + 1);
				}

				close(fd_find);
			}
			else if(*command == '1')
			{
				sprintf(line, "%s %s_f %s %s %d", client_name, cmds, files, path, getpid());
				write(fd, line, strlen(line) + 1);

				signal(SIGUSR1, sucess);
				signal(SIGUSR2, non_sucess);

				pause();
					
				if(SIGNAL == 0)
				{
					sprintf(msg, "%s: pasta copiada\n", files);
					write(1, msg, strlen(msg) + 1);
				}
				else if(SIGNAL == 1)
				{
					sprintf(msg, "%s: pasta não copiada\n", files);
					write(1, msg, strlen(msg) + 1);
				}
				SIGNAL = -1;
			}
		}
		else if (strncmp(cmds, "restore", 7) == 0)
		{
			cmds = strtok(cmds, " ");
			files = strtok(NULL, "\n");

			sprintf(line, "%s %s %s %s %d", client_name, cmds, files, path, getpid());	
			write(fd, line, strlen(line) + 1);

			signal(SIGUSR1, sucess);
			signal(SIGUSR2, non_sucess);
						
			pause();

			if(SIGNAL == 0)
			{
				sprintf(msg, "%s: recuperado\n", files);
				write(1, msg, strlen(msg) + 1);
			}
			else if(SIGNAL == 1)
			{
				sprintf(msg, "%s: não recuperado\n", files);
				write(1, msg, strlen(msg) + 1);
			}
			SIGNAL = -1;	
		}
		else if(strncmp(cmds, "delete", 6) == 0)
		{
			cmds = strtok(cmds, " ");
			files = strtok(NULL, "\n");

			sprintf(line, "%s %s %s %s %d", client_name, cmds, files, path, getpid());
			write(fd, line, strlen(line) + 1);

			signal(SIGUSR1, sucess);
			signal(SIGUSR2, non_sucess);
						
			pause();

			if(SIGNAL == 0)
			{
				sprintf(msg, "%s: eliminado\n", files);
				write(1, msg, strlen(msg) + 1);
			}
			else if(SIGNAL == 1)
			{
				sprintf(msg, "%s: não eliminado\n", files);
				write(1, msg, strlen(msg) + 1);
			}
			SIGNAL = -1;
		}
		else if(strncmp(cmds, "gc", 2) == 0)
		{
			sprintf(line, "%s gc . %s %d", client_name, strtok(path, "\n"), getpid());
			write(fd, line, strlen(line) + 1);
		}
	
		/* De forma a facilitar a visualização podemos imprimir um newline em excesso. */
		write(1, "\n", 1);
		write(1, str, strlen(str));
	}
	close(fd_error);
	clearEcran();
}

int main()
{
	char *usr, *pass, *str, *line, option, c;
	int fd, boolean, i, j, n;
	
	usr = malloc(SIZE_LOGIN * sizeof(char));
	pass = malloc(SIZE_LOGIN * sizeof(char));
	line = malloc(SIZE * sizeof(char));

	getlogin_r(line, SIZE);
	sprintf(CREDENTIALS, "/home/%s/.Backup/.credentials.nfo", line);
	
	str = "Press 1 to Sign In\nPress 2 to Sign Up\nPress 0 to Quit\n";
	write(1, str, strlen(str) + 1);
	read(0, &option, 2); /* O segundo char é para o '\n'. */
	
	clearEcran();

	while(option != '0') {

		if(option == '1')
		{
			str = "Insira o nome de utilizador (máx. 32 caracteres):\n";
			write(1, str, strlen(str));		
			read(0, usr, SIZE_LOGIN + 1);

			str = "Insira a password (máx. 32 caracteres):\n";
			write(1, str, strlen(str));
			read(0, pass, SIZE_LOGIN + 1);

			fd = open(CREDENTIALS, O_RDONLY);
	
			i = boolean = 0;
			while(read(fd, &c, 1) > 0 && boolean == 0)
			{	
				if(c == ' ' && usr[i] == '\n') /* Se chegou a um espaço então toda a string do user estava correcta.	*/
				{		
					i = 0;
					while(read(fd, &c, 1) > 0 && boolean == 0) /* Testar a palavra passe. */
					{
						if(pass[i] != c) /* Nunca sai do limite da string pass visto falhar quando pass[i] = '\0'. */
						{
							i = 0;
							while(read(fd, &c, 1) > 0 && c != '\n'); /* Avançar para o fim da linha. */
						}
						else if(c == '\n')
						{
							boolean = 1;
						}
						else 
						{
							i++;
						}
					}	
				}
				else if(usr[i] != c)
				{
					i = 0; /* Falhou, logo recomeçar a leitura da string a comparar. */
					while(read(fd, &c, 1) > 0 && c != '\n'); /* Avançar para o fim da linha. */
				}
				else
				{ 
					i++;					
				}
			}

			close(fd);
			clearEcran();
			if (boolean == 1)
			{
				str = "Permissão concedida!\n\n";
				write(1, str, strlen(str));
			 	 
			 	access_server(strtok(usr, "\n"));
			}	
			else
			{
				str = "Permissão negada!\n\n";
				write(1, str, strlen(str));
			}
		}
		else if(option == '2') 
		{
			clearEcran();
			str = "Insira o nome de utilizador (máx. 32 caracteres):\n";
			write(1, str, strlen(str));
			n = read(0, usr, SIZE_LOGIN + 1);
			
			while(isInputOk(usr, n) == 0)
			{
				clearEcran();
				str = "O nome de utilizador não pode conter espaços. Insira novamente:\n";
				write(1, str, strlen(str));
				n = read(0, usr, SIZE_LOGIN + 1);
			}
			if(existsUser(usr) == 0)
			{
				str = "Insira a password (máx. 32 caracteres):\n";
				write(1, str, strlen(str));
				n = read(0, pass, SIZE_LOGIN + 1);

				while(isInputOk(pass, n) == 0)
				{
					clearEcran();
					str = "A palavra passe não pode conter espaços. Insira novamente:\n";
					write(1, str, strlen(str));
					n = read(0, pass, SIZE_LOGIN + 1);
				}

				fd = open(CREDENTIALS, O_CREAT | O_APPEND | O_WRONLY, 0666);
				
				for(i = 0; usr[i] != '\n'; i++)
				{
					line[i] = usr[i];
				}
				line[i++] = ' ';
				for(j = 0; pass[j] !='\n'; i++, j++)
				{
					line[i] = pass[j];
				}
				line[i] = '\n';

				write(fd, line, i + 1);
				
				close(fd);

				clearEcran();
				str = "Novo utilizador adicionado com sucesso!\n\n";
				write(1, str, strlen(str));
			}
			else
			{
				clearEcran();
				str = "Utilizador já existe!\n\n";
				write(1, str, strlen(str));
			}
		}
		
		str = "Press 1 to Sign In\nPress 2 to Sign Up\nPress 0 to Quit\n";
		write(1, str, strlen(str));
		read(0, &option, 2);
	}	 
	
	free(usr);
	free(pass);
	free(line);

	return 0;
}
