#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>

#define SIZE 512
#define MAX_CHLD 5 /* Número máximo de processos filhos permitidos em simultâneo. */
#define FIFO_PATHNAME "eve"

unsigned int how_many = 0; /* Variável para controlo do número de processos a serem
                            * executados concorrentemente. */

ssize_t readln(int fd, char *buffer, size_t nbyte)
{
	int i;
	for(i = 0; read(fd, buffer + i, 1) == 1 && buffer[i] != '\n' && i < (int)nbyte; i++);
	buffer[i] = '\0';	/* Pretendemos terminar a string no '\n' exclusivé no contexto deste programa. */
	return i;
}

/* Verificar se um dado ficheiro existe em "metadata" para um dado cliente. */
int existsInMeta(char* client, char* file)
{
	int fd[2], fd_error, r = 0;
	char *aux;

	aux = malloc(SIZE * sizeof(char));

	sprintf(aux, "metadata/%s", client);

	if(pipe(fd) == -1)
	{
		return -1; /* Um erro ocorreu. */
	}

	fd_error = open("stderr", O_CREAT | O_APPEND | O_WRONLY, 0666);

	if(fork() == 0)
	{
		dup2(fd[1], 1);
		close(fd[1]);
		dup2(fd_error, 2);
		close(fd_error);

		close(fd[0]);

		r = execlp("ls", "ls", "-1", aux, NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		close(fd[1]);
		
		if(r == -1)
		{
			return 0;
		}
	}

	r = 0;
	/* Percorrer todos os links do cliente, verificar se o nome coincide. */
	while(readln(fd[0], aux, SIZE) > 0 && r == 0)
	{
		if(strncmp(aux, file, strlen(file)) == 0)
		{
			r = 1;
		}
	}
	close(fd[0]);

	free(aux);
	return r;	
}

/** 
 * Converter o nome de um ficheiro acompanhado do seu caminho para outro possível de
 * utilizar ao guardar o ficheiro em disco.
 */
void pathToFile(char *path_name)
{
	int i;
	if(path_name != NULL)
	{
		for(i = 0; path_name[i] != '\0'; i++)
		{
			if(path_name[i] == '/')
			{
				path_name[i] = ',';
			}
		}
	}
}

/** 
 * Função que salvaguarda um dado ficheiro. Recebe o nome do cliente (client) que pretende salvar
 * o ficheiro (file). É necessário receber o caminho (path) onde se encontra o ficheiro.
 * Se isFolder for 0 então trata-se apenas do nome de um ficheiro proveniente de um comando simples:
 * "backup file.txt". Caso seja 1, trata-se de um ficheiro juntamente com o seu caminho (em file). 
 */
int backup(char *client, char *file, char *path, int isFolder)
{
	int fd, fd_error, r = 0;
	char *SHA, *aux, *aux2;

	SHA = malloc(SIZE * sizeof(char));
	aux = malloc(SIZE * sizeof(char));
	aux2 = malloc(SIZE * sizeof(char));

		fd = open("tmp", O_CREAT | O_TRUNC | O_WRONLY, 0666);
		fd_error = open("stderr", O_CREAT | O_APPEND | O_WRONLY);

		if(fork() == 0)
		{
			dup2(fd, 1); /* Redirecionar o output de sha1sum para um ficheiro tmp. */
			close(fd);
			dup2(fd_error, 2); /* Redirecionar todos os erros. */
			close(fd_error);

			sprintf(aux, "%s/%s", path, file);
			r = execlp("sha1sum", "sha1sum", aux, NULL);
			_exit(1);
		}
		else
		{
			wait(NULL);
			close(fd);
			if (r != 0) return r; /* Caso tenha corrido algum erro, encerrar a função com mensagem de erro. */
		}

		fd = open("tmp", O_RDONLY);
		read(fd, SHA, SIZE);
		close(fd);

		SHA = strtok(SHA, " ");

		if(fork() == 0)
		{
			dup2(fd_error, 2);
			close(fd_error);

			sprintf(aux, "%s/%s", path, file);

			/* Como se trata de um "backup" devemos manter os ficheiros originais com a flag "-k". */
			r = execlp("gzip", "gzip", "-k", aux, NULL);
			_exit(1);
		}
		else
		{
			wait(NULL);
			if (r != 0) return r;
		}

		if(fork() == 0)
		{
			dup2(fd_error, 2);
			close(fd_error);

			memcpy(aux, "data/", 6);

			sprintf(aux2, "%s/%s.gz", path, file);

			/* Mover o ficheiro já comprimido para "data". */
			r = execlp("mv", "mv", aux2, strcat(aux, SHA), NULL);
			_exit(1);
		}
		else
		{
			wait(NULL);
			if (r != 0) return r;
		}

		if(fork() == 0)
		{
			dup2(fd_error, 2);
			close(fd_error);

			memcpy(aux, "metadata/", 10);
			
			/* Criar a pasta utilizador em metadata, ter em atenção que se caso já existir mkdir apenas
			 * emite uma mensagem de erro. */
			execlp("mkdir", "mkdir", strcat(aux, client), NULL); 
			_exit(1);
		}
		else
		{
			wait(NULL);
		}

		/* Se file for proveniente de uma pasta, devemos converter determinados caracteres do seu nome. */
		if(isFolder == 1)
		{
			pathToFile(file);
		}

		/* Criação do link a apontar para o conteúdo em data. */
		if(fork() == 0)
		{
			dup2(fd_error, 2);
			close(fd_error);

			memcpy(aux, "metadata/", 10);
			aux = strcat(aux, client);
			aux = strcat(aux, "/");

			memcpy(aux2, "../../data/", 12);
						
			r = execlp("ln", "ln", "-s", strcat(aux2, SHA), strcat(aux, file), NULL);
			_exit(1);
		}
		else
		{
			wait(NULL);
			if (r != 0) return r;
		}
		free(SHA);
		free(aux);
		free(aux2);

return 0;	
}

/**
 * Backup que salvaguarda pastas, recebe como parâmetros o nome do utilizador, a pasta a guardar
 * e o caminho onde a mesma se encontra.
 */
int backup_f(char *client, char *folder, char *path)
{
	int p_files[2], r = 0;
	char *buffer;

	buffer = malloc(SIZE * sizeof(char));

	if(pipe(p_files) == -1)
	{
		return -1;
	}

	if(fork() == 0)
	{
		dup2(p_files[1], 1);
		close(p_files[1]);
		close(p_files[0]);

		/* Obter todos os caminhos de todos os ficheiros de folder. */
		sprintf(buffer, "find %s/%s -type f", path, folder);
		r = execlp("/bin/sh", "sh", "-c", buffer, NULL); /* Listar todos os ficheiros. */
		_exit(1);
	}
	else
	{
		wait(NULL);
		close(p_files[1]);
		if(r == -1)
		{
			return r;
		}
	}
	
	/* Ler todos os ficheiros, linha a linha. */
	while(readln(p_files[0], buffer, SIZE) > 0)
	{
		/* Necessário avançar a string, só queremos a partir de folder para a frente.
		 * Invocamos um backup simples sobre ficheiros com o último parâmetro a 1,
		 * indicando que se trata de um ficheiro proveniente de uma directoria. 
		 */
		r = backup(client, buffer + strlen(path) + 1, path, 1);
		if(r == -1)
		{
			return r;
		}
	}
	close(p_files[0]);

	free(buffer);
	return r;
}

/**
 * Esta função restaura ficheiros individuais.
 * Recebe o nome do utilizador, o nome do ficheiro e por fim, o caminho onde
 * o utilizador se encontra.
 */
int restore(char *client, char *file, char *path)
{
	int fd_error, r = 0;
	char *aux, *aux2;

	aux = malloc(SIZE * sizeof(char));
	aux2 = malloc(SIZE * sizeof(char));

	/* Se o ficheiro não existir, não existe nada para restaurar e encerramos
	 * a função com um sinal de erro. */
	if(existsInMeta(client, file) == 0)
	{
		return -1;
	}

	memcpy(aux, "metadata/", 10);
	aux = strcat(aux, client);
	aux = strcat(aux, "/");
	strcat(aux, file);
	memcpy(aux2, aux, strlen(aux) + 1);

	fd_error = open("stderr", O_CREAT | O_APPEND | O_WRONLY);

	if(fork() == 0)
	{
		dup2(fd_error, 2);
		close(fd_error);
		
		/* O link não tem extensão ".gz" sendo necessário renomeá-lo. */
		r = execlp("mv", "mv", aux, strcat(aux2,".gz"), NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		if (r != 0) return r;
	}

	if(fork() == 0)
	{
		dup2(fd_error, 2);
		close(fd_error);

		/* Utilizamos as flags "-fd" de forma a forçar a descompressão de links.
		 * Contudo, é o ficheiro original que vai sofrer uma descompressão, ficamos
		 * com acesso ao seu conteúdo. */
		r = execlp("gzip", "gzip", "-fd", strcat(aux,".gz"), NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		if (r != 0) return r;
	}

	if (fork() == 0)
	{
		dup2(fd_error, 2);
		close(fd_error);

		strcat(path, "/");

		/* Por fim, mover para a directoria do utilizador.
		 * Já existirá a entrada desse ficheiro em metadata. */
		r = execlp("mv", "mv", aux, strcat(path, file), NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		close(fd_error);
		if (r != 0) return r;
	}

	free(aux);
	free(aux2);

	return 0;
}

/**
 * Restore de pastas, utiliza um processo inicial diferente de restore.
 */
int restore_f(char *client, char *folder, char *path)
{
	int fd_error, p[2], i, ctrl, flag, r = 0;
	char *buffer, aux[SIZE], path_name[SIZE];

	buffer = malloc(SIZE * sizeof(char));

	fd_error = open("stderr", O_CREAT | O_APPEND | O_WRONLY, 0666);

	if(pipe(p) == -1)
	{
		return -1;
	}

	sprintf(aux, "metadata/%s/", client);
	ctrl = strlen(aux); /* O que vamos querer avançar posteriormente. */
	sprintf(buffer, "find %s%s,*", aux, folder);

	if(fork() == 0)
	{
		dup2(fd_error, 2);
		close(fd_error);

		dup2(p[1], 1);
		close(p[1]); 
		close(p[0]);
		
		r = execlp("/bin/sh", "sh", "-c", buffer, NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		close(p[1]);
		if(r == -1)
		{
			return r;
		}
	}

	/* Ler todas as sub-pastas, e para cada uma recriar a mesma. */
	flag = 0; /* Apenas para verificar se a pasta pedida para guardar existe. */
	while(readln(p[0], buffer, SIZE) > 0)
	{
		flag = 1; /* Se entrou no ciclo pelo menos uma pasta existe. */
		for(i = 0; *(buffer + i + ctrl) != '\0'; i++)
		{
			if(*(buffer + i + ctrl) != ',')
			{
				path_name[i] = *(buffer + i + ctrl);
			}
			else
			{
				if(fork() == 0)
				{
					path_name[i] = '\0';

					dup2(fd_error, 2);
					close(fd_error);

					sprintf(aux, "%s/%s", path, path_name);
					execlp("mkdir", "mkdir", aux, NULL);
					_exit(1);
				}
				else
				{
					wait(NULL);
				}

				path_name[i] = '/';
			}
		}
		path_name[i] = '\0';
		
		/* Se todas as pastas foram recriadas, procedemos com a recuperação dos ficheiros. */
		memcpy(aux, buffer, SIZE);

		if(fork() == 0)
		{	
			r = execlp("mv", "mv", aux, strcat(buffer,".gz"), NULL);
			_exit(1);
		}
		else
		{
			wait(NULL);
			if(r == -1)
			{
				return r;
			}
		}

		if(fork() == 0)
		{
			r = execlp("gzip", "gzip", "-fd", strcat(aux,".gz"), NULL);
			_exit(1);
		}
		else
		{
			wait(NULL);
			if(r == -1)
			{
				return r;
			}
		}
		
		if (fork() == 0)
		{
			strcat(path, "/");
						
			r = execlp("mv", "mv", buffer, strcat(path, path_name), NULL);
			_exit(1);
		}
		else
		{
			wait(NULL);
			if(r == -1)
			{
				return r;
			}
		}
		/* Avançar para o próximo ficheiro na próxima iteração do ciclo. */
	}
	close(p[0]);
	
	/* Se a pasta não existir então devemos encerrar o programa com sinal de erro. */
	if(flag == 0)
	{
		r = -1;
	}

	close(fd_error);
	return r;
}

/**
 * Função de eliminação de ficheiros, sempre que possível tenta eliminar a sua entrada em data.
 * Caso o ficheiro seja partilhado, apenas remove o link respectivo de metadata. Tivemos em
 * atenção que um ficheiro pode ser partilhado dentro do mesmo utilizador.
 */
int delete(char *client, char *file)
{
	int fd[2], fd_error, fd_sha[2], r = 0, s = 0;
	char *path, *SHA, *other_SHA, *other_client, *path_client, *other_file;
	
	path = malloc(SIZE * sizeof(char));
	SHA = malloc(SIZE * sizeof(char));
	other_SHA = malloc(SIZE * sizeof(char));
	other_client = malloc(SIZE * sizeof(char));
	path_client = malloc(SIZE * sizeof(char));
	other_file = malloc(SIZE * sizeof(char));

	/* Se o ficheiro ou directoria a recuperar não existir, não devemos proceder com trabalho. */
	if(existsInMeta(client, file) == 0)
	{
		return -1;
	}

	if(pipe(fd) == -1)
	{
		return -1;
	}

	fd_error = open("stderr", O_CREAT | O_APPEND | O_WRONLY);

	if(fork() == 0)
	{
		dup2(fd[1], 1);
		close(fd[1]); 
		close(fd[0]);
		
		sprintf(path, "metadata/%s/%s", client, file);
		s = execlp("readlink", "readlink", path, NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		close(fd[1]);
		
		if (s != 0) return s;
		
		while(read(fd[0], SHA, SIZE) > 0);
		close(fd[0]);
	}
	
	if(pipe(fd) == -1)
	{
		return -1;
	}
	
	if (fork() == 0)
	{
		dup2(fd[1], 1);
		close(fd[0]);
		close(fd[1]);
		
		dup2(fd_error, 2);
		close(fd_error);

		s = execlp("ls", "ls", "-1", "metadata", NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		close(fd[1]);
		
		if (s != 0) return s;
		while(readln(fd[0], other_client, SIZE) > 0)
		{
			if(pipe(fd_sha) == -1)
			{
				return -1;
			}

			if(fork() == 0)
			{
				dup2(fd_sha[1], 1);
				close(fd_sha[1]);
				close(fd_sha[0]);
				dup2(fd_error, 2);
				close(fd_error);
				sprintf(path_client, "metadata/%s", other_client);

				s = execlp("ls", "ls", "-1", path_client, NULL);
				_exit(1);
			}
			else
			{
				wait(NULL);
				close(fd_sha[1]);
				if (s != 0) return s;
				while(readln(fd_sha[0], other_file, SIZE) > 0)
				{
					sprintf(path_client, "metadata/%s/%s", client, other_file);

					readlink(path_client, other_SHA, SIZE);
					
					if(strcmp(other_SHA, strtok(SHA, "\n")) == 0 && strcmp(other_client, client) != 0)
					{
							r = 1; /* Existe algum link que não o próprio a apontar para o ficheiro. */
							break;
					}
					/* Caso estejamos a olhar para o mesmo cliente mas ficheiros de nome diferente com o mesmo digest. */
					else if(strcmp(other_SHA, strtok(SHA, "\n")) == 0 && 
						      strcmp(other_client, client) == 0 && strcmp(file, other_file) == 0)
					{
						r = 1;
						break;
					}
				}
				close(fd_sha[0]);
			}
			if (r == 1) break;
		}
		close(fd[0]);
	}
	
	/* Remover a entrada em metadata, esta operação é sempre executada quando o ficheiro existe. */
	if(fork() == 0)
	{
		dup2(fd_error, 2);
		close(fd_error);
		sprintf(path_client, "metadata/%s/%s", client, file);
		s = execlp("rm", "rm", path_client, NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		if (s != 0) return s;
	}

	if (r == 0)
	{
		if(fork() == 0)
		{
			dup2(fd_error, 2);
			close(fd_error);

			strtok(SHA, "/");
			strtok(NULL, "/");
			SHA = strtok(NULL, "\0");
			
			s = execlp("rm", "rm", SHA, NULL);
			_exit(1);
		}
		else
		{
			wait(NULL);

			if (s != 0) return s;
		}
	}
	
	free(other_file);
	free(path_client);
	free(path);
	free(SHA);
	free(other_SHA);
	free(other_client);
	
	return 0;
}

/**
 * Função delete para pastas, é necessário anteriormente listar todos os ficheiros pertencentes
 * a folder de forma a posteriormente os eliminar de data, se possível.
 */
int delete_f(char *client, char *folder)
{
	int fd_error, p_files[2], r = 0, size, flag;
	char *buffer, *aux;

	buffer = malloc(SIZE * sizeof(char));
	aux = malloc(SIZE * sizeof(char));

	fd_error = open("stderr", O_CREAT | O_TRUNC | O_WRONLY, 0666);

	if(pipe(p_files) == -1)
	{
		return -1;
	}

	sprintf(aux, "metadata/%s/", client);
	size = strlen(aux);

	/* Listar todos os ficheiros que estamos a tentar eliminar. */
	sprintf(buffer, "find %s%s,*", aux, folder);

	if(fork() == 0)
	{
		dup2(fd_error, 2);
		close(fd_error);

		/* Redirecionar o output do execlp para o pipe. */
		dup2(p_files[1], 1);
		close(p_files[1]);
		close(p_files[0]);

		/* Listar todos os ficheiros em pastas. */
		r = execlp("/bin/sh", "sh", "-c", buffer, NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		close(p_files[1]);
		if(r == -1)
		{
			return r;
		}
	}
	
	flag = 0; /* Verificar se o pipe recebe o output do execlp significando se existem ficheiros. */
	while(readln(p_files[0], buffer, SIZE) > 0)
	{
		flag = 1;
		/* Necessário avançar a string, apenas queremos o nome do ficheiro. */
		r = delete(client, buffer + size); /* Invocar um delete simples sobre o ficheiro. */
		if(r == -1)
		{
			return r;
		}
	}
	close(p_files[0]);
	close(fd_error);

	free(buffer);
	free(aux);
	return flag == 1 ? 0 : -1;
}

/**
 * Função que tem o intuito de remover as entradas em data obsoletas.
 */
void gc()
{
	int fd, fd_error, fd_pipe[2], fd_files, flag = 0;
	char *buffer_line, *client_line, *file_line, *buff, str[SIZE];

	buffer_line = malloc(SIZE * sizeof(char));
	client_line = malloc(SIZE * sizeof(char));
	file_line = malloc(SIZE * sizeof(char));
	buff = malloc(SIZE * sizeof(char));

	/* Inicialmente, listar todos os clientes em metadata de forma a evitar fazer este trabalho várias vezes consecutivas
	 * (tantas quantas as chaves de data) ao longo do algoritmo. */

	fd = open(".users", O_CREAT | O_TRUNC | O_WRONLY, 0666);
	fd_error = open("stderr", O_CREAT | O_APPEND | O_WRONLY, 0666);

	if(fork() == 0)
	{
		dup2(fd, 1);

		dup2(fd_error, 2);
		close(fd_error);

		execlp("ls", "ls", "-1", "metadata", NULL);
		_exit(1); /* Caso tenha ocorrido algum erro durante a execução e tenha voltado à função,
		           * saimos do processo filho. */
	}
	else
	{
		wait(NULL);
		close(fd);
	}

	/* Listar todas as chaves em data. Através de um pipe anónimo entre a thread filho e a pai, enviamos essa informação.
	 * O pipe irá ler, linha a linha, o output do filho após o execlp. */

	if(pipe(fd_pipe) == -1)
	{
		return;
	}

	if(fork() == 0)
	{
		dup2(fd_pipe[1], 1);
		close(fd_pipe[1]);
		close(fd_pipe[0]);

		execlp("ls", "ls", "-1", "data", NULL);
		_exit(1);
	}
	else
	{
		wait(NULL);
		close(fd_pipe[1]);
	}

	while(readln(fd_pipe[0], buffer_line, SIZE) > 0) /* À "saída" do pipe ler todas as chaves. */
	{
		fd = open(".users", O_RDONLY); /* Para cada chave verificar se não existe cliente que esteja a utilizar o ficheiro. */
		
		while(readln(fd, client_line, SIZE) > 0 && flag == 0) /* Percorrer todos os clientes. */
		{
			fd_files = open(".files", O_CREAT | O_TRUNC | O_WRONLY, 0666);

			sprintf(str, "metadata/%s", client_line);

			if(fork() == 0)
			{
				dup2(fd_files, 1);

				execlp("ls", "ls", "-1", str, NULL); /* Determinar todos os ficheiros de um dado cliente. */
				_exit(1);
			}
			else
			{
				wait(NULL);
				close(fd_files);
			}

			fd_files = open(".files", O_RDONLY);

			while(readln(fd_files, file_line, SIZE) > 0 && flag == 0)
			{
				sprintf(str, "metadata/%s/%s", client_line, file_line);
				readlink(str, buff, SIZE);
				
				strtok(buff, "/"); 
				strtok(NULL, "/");
				strtok(NULL, "/");
				buff = strtok(NULL, "\0");

				if(strcmp(buff, buffer_line) == 0)
				{
					flag = 1; /* Existe. */
				}
			}
			close(fd_files);
		}
		close(fd);

		if(flag == 0)
		{
			if(fork() == 0)
			{
				sprintf(str, "data/%s", buffer_line);
				execlp("rm", "rm", str, NULL);
				_exit(1);
			}
			else
			{
				wait(NULL);
			}
		}
		flag = 0;	
	}
	close(fd_pipe[0]);

free(buffer_line);
free(client_line);
free(file_line);
}

/* Handler simples. */
void signal_handler(int s)
{

}

int main()
{
	char *buffer, *client, *cmd, *file, *msg, *path;
	int fd, client_pid, ctrl;
	
	buffer = malloc(SIZE * sizeof(char));
	path = malloc(SIZE * sizeof(char));

	mkfifo(FIFO_PATHNAME, 0666);

	fd = open(FIFO_PATHNAME, O_RDONLY);

	while(read(fd, buffer, SIZE) > 0)
	{
		if(how_many >= MAX_CHLD)
		{	
				signal(SIGCHLD, signal_handler);
				pause();
		}
		how_many++;
		if(fork() == 0)
		{	
			client = strtok(buffer, " ");
			cmd = strtok(NULL, " ");
			file = strtok(NULL, " ");
			path = strtok(NULL, " ");
			client_pid = atoi(strtok(NULL, "\0"));

			if(cmd != NULL && file != NULL)
			{
				if(strcmp(cmd, "backup") == 0)
				{
					if(backup(client, file, path, 0) == 0)
					{
						kill(client_pid, SIGUSR1); /* SUCESSO */
					}
					else
					{
						kill(client_pid, SIGUSR2); /* INSUCESSO */
					}
				}
				else if(strcmp(cmd, "restore") == 0)
				{	
					ctrl = restore_f(client, file, path);

					/* Após restaurarmos a pasta, tentaremos proceder para algum ficheiro. */	
					if(restore(client, file, path) == 0 || ctrl == 0)
					{
						kill(client_pid, SIGUSR1); /* Se foi restaurada uma pasta ou um ficheiro. */
					}
					else 
					{
						kill(client_pid, SIGUSR2); /* Não foi possível restaurar nenhum dos dois. */
					}
				}
				else if(strcmp(cmd,"delete") == 0)
				{
					ctrl = delete_f(client, file);

					if(delete(client, file) == 0 || ctrl == 0)
					{
						kill(client_pid, SIGUSR1);
					}
					else
					{
						kill(client_pid, SIGUSR2);
					}
				}
				else if(strcmp(cmd, "gc") == 0)
				{
					gc();
				}
				else if(strcmp(cmd, "backup_f") == 0)
				{
					if(backup_f(client, file, path) == 0)
					{
						kill(client_pid, SIGUSR1); /* Pasta copiada. */
					}
				else
				{
					kill(client_pid, SIGUSR2); /* Pasta não copiada, erro ocorreu. */
				}
			}
		}
		else
		{
			msg = "Erro: operação inválida!\n";
			write(1, msg, strlen(msg));
		}
			kill(getppid(), SIGCHLD);
			_exit(1);
		}
		/* Já fora do processo filho, podemos decrementar o número de operações concorrentes. */
		if (how_many > 0) how_many--;
	}

	free(buffer);
	free(path);

	return 0;
}