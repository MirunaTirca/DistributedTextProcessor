#include "mpi.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define min(X, Y) (((X) < (Y)) ? (X) : (Y))

char *input;

// daca ok_start_paragraph[i] == 1 inseamna ca thread-ul i citeste un
// paragraf destinat lui (hthread 0 -> horror, thread 1 -> comedy,
// thread 2-> fantasy, thread 3 -> science-fiction)
int ok_start_paragraph[] = {0, 0, 0, 0};

// fiecare thread i retine in nb_lines_paragraph[i] numarul de linii
// al paragrafului curent
int nb_lines_paragraph[] = {0, 0, 0 , 0};

// fiecare thread i retine in order_paragraphs[i] al catelea
// paragraf din fisier este paragraful curent
int order_paragraphs[] = {0, 0, 0, 0};

// matrici folosite in citire pt a retine paragrafe
// thread-ul 0 va retine (rand pe rand) un paragraf horror in char **horror_paragraph
char **horror_paragraph;
// thread-ul 1 va retine (rand pe rand) un paragraf comedy in char **comedy_paragraph
char **comedy_paragraph;
// thread-ul 2 va retine (rand pe rand) un paragraf fantasy in char **fantasy_paragraph
char **fantasy_paragraph;
// thread-ul 3 va retine (rand pe rand) un paragraf sf in char **science_fiction_paragraph
char **science_fiction_paragraph;

// numarul de pargrafe de fiecare fel din fisier
int nb_par_horror = 0, nb_par_comedy = 0, nb_par_fantasy = 0, nb_par_sf = 0;

// briera pentru a sincroniza citirea paralela din fisier
pthread_barrier_t barrier;

// folosit de thread-urile Reader ale fiecarui Worker pentru a 
//trimite pe rand paragrafele procesate inapoi la master
pthread_mutex_t mutex_send;

// structura in care o sa retin paragraf cu paragraf textul modificat
struct Paragraph {
	int type; // 1 = horror; 2 = comedy; 3 = fantasy; 4 = sf; -> din sursa mesajului primit de master
	int order; // din tag-ul mesajului primit de master
	int nb_lines; // numarul de linii din paragraf
	char** text; // paragraful
};

// sortez paragrafele in ordine crescatoare dupa ordine
int compare_function(const void *a,const void *b) {
	struct Paragraph *x = (struct Paragraph *) a;
	struct Paragraph *y = (struct Paragraph *) b;
	return x->order - y->order;
}

char* process_horror_line (char *s) {
	// consoanele dublate - dublarea se face doar in minuscula
	char *new_line = malloc(2 * strlen(s)* sizeof(char));
	int k = 0;
	for (int i = 0; i < strlen(s); i++) {
		if (s[i] == 'a' || s[i] == 'e' || s[i] == 'o' || s[i] == 'i' || s[i] =='u' ||
			s[i] == 'A' || s[i] == 'E' || s[i] == 'O' || s[i] == 'I' || s[i] == 'U') {
			new_line[k++] = s[i];
		} else {
			new_line[k++] = s[i];
			if (s[i] >= 'a' && s[i] <= 'z') {
				new_line[k++] = s[i];
			} else if (s[i] >= 'A' && s[i] <= 'Z') {
				new_line[k++] = s[i] + 32;
			}
		}
	}
	new_line[k++] = '\0';
	return new_line;
}

char* process_comedy_line (char *s) {
	// fiecare litera de pe pozitie para sa fie facuta majuscula
	// numeroatarea incepe per cuvant si incepe de la 1
	int poz = 0;	
	for (int i = 0 ; i < strlen(s); i++) {
		poz++;
		if (s[i] == ' ') {
			poz = 0;
		}
		else {
			if (poz % 2 == 0 && s[i] >= 'a' &&s[i] <= 'z') {
				s[i] -= 32;
			}
		}
	}
	return s;
}

char* process_fantasy_line (char *s) {
	// prima litera a fiecarui cuvant facuta majuscula
	int ok = 1;
	for (int i = 0; i < strlen(s); i++) {
		if (s[i] == ' '){
			ok = 1; // incepe cuvant nou			
		} else {
			if (ok == 1) {
				//cuvant nou
				if (s[i] >= 'a' && s[i] <= 'z') {
					s[i] = s[i] - 32;
				}
				ok = 0;
			}
		}
	}
	return s;
}

char* process_sf_line (char *s) {
	// fiecare al 7lea cuvant inversat
	// orice cuvant e despartit prin " " sau "\n"
	int count = 1, i = 0;
	int ok = 0;
	int start, end;
	while(i < strlen(s)) {
		if (s[i] == ' ' || s[i] == '\n') {
			count ++;
			i++;
		}
		else {
			if (count % 7 == 0) {
				// incepe un cuvant care trebuie inversat
				start = i;
				end = i + 1; 
				while(end < strlen(s) && (s[end] != ' ' && s[end] != '\n')){
					end++;					
				}
				end--; // s[end] este ultima litera din cuvantul de inversat
				for (int j = start; j <= (end + start) / 2; j++) {
					char aux = s[j];
					s[j] = s[end-j+start];
					s[end-j+start] = aux;
				}
				i = end+1;
			} else {
				i++;
			}
		}
	}
	return s;
}

// citirea in paralel din fisier
void *f_read_file(void *arg) {
	long id = (long)arg;
	FILE *fp;
	fp = fopen(input, "r");
	char * line = NULL;
	size_t len = 0;
	if (fp == NULL) {
		exit(1);
	}
	while (getline(&line, &len, fp) != -1) {		
		if (id == 0) {
			// thread-ul 0 se opcupe de paragrafele HORROR
			if (strcmp (line, "\n") == 0) {
				// s-a terminat un paragraf
				ok_start_paragraph[id] = 0;
				// daca paragraful era horror - trebuie trimis spre worker-ul horror
				if (nb_lines_paragraph[id] != 0) {
					nb_par_horror++;
					// trimit paragraful la worker-ul HORROR - rank 1
					// mesajele vor avea ca tag numarul de ordine din fisier
					// (al catelea paragraf este in input)

					// intai trimit nr de linii al paragrafului
					MPI_Send(&nb_lines_paragraph[id], 1, MPI_INT, 1, order_paragraphs[id], MPI_COMM_WORLD);
					// paragraful
					for (int i = 0; i < nb_lines_paragraph[id]; i++) {
						// inainte trimit si nr de caractere de pe linie
						int line_len = strlen(horror_paragraph[i]);
						MPI_Send(&line_len, 1, MPI_INT, 1, order_paragraphs[id], MPI_COMM_WORLD);
						//linia
						MPI_Send(horror_paragraph[i], line_len, MPI_CHAR,
						 1, order_paragraphs[id], MPI_COMM_WORLD);
					}
				}	
				// eliberez memoria
				for (int i = 0 ; i < nb_lines_paragraph[id]; i++) {
					free(horror_paragraph[i]);
				}
				// reactualizez nb_lines_paragraph[id] si order_paragraphs[id]
				nb_lines_paragraph[id] = 0;
				order_paragraphs[id]++;
			}
			if (ok_start_paragraph[id] == 0 && 
				strcmp(line, "horror\n") == 0) {
				// incepe paragraf nou de horror
				ok_start_paragraph[id] = 1;
			}
			else if (ok_start_paragraph[id] == 1) {
				// citesc paragraf horror si il retin
				horror_paragraph[nb_lines_paragraph[id]] = malloc((strlen(line)+1)*sizeof(char));
				strcpy(horror_paragraph[nb_lines_paragraph[id]], line);
				nb_lines_paragraph[id]++;
			}
		}
		else if (id == 1){
			// COMEDY
			if (strcmp (line, "\n") == 0) {
				// s-a terminat un paragraf
				ok_start_paragraph[id] = 0;
				// daca era paragraf comedy trebuie trimis
				if (nb_lines_paragraph[id] != 0){
					nb_par_comedy++;
					// trimit paragraful la worker-ul COMEDY - rank 2
					// mesajele vor avea ca tag numarul de ordine din fisier
					// (al catelea paragraf este in input)

					// intai trimit nr de linii al paragrafului
					MPI_Send(&nb_lines_paragraph[id], 1, MPI_INT, 2, order_paragraphs[id], MPI_COMM_WORLD);
					// paragraful
					for (int i = 0; i < nb_lines_paragraph[id]; i++) {
						// inainte trimit si nr de caractere de pe linie
						int line_len = strlen(comedy_paragraph[i]);
						MPI_Send(&line_len, 1, MPI_INT, 2, order_paragraphs[id], MPI_COMM_WORLD);
						//linia
						MPI_Send(comedy_paragraph[i], line_len, MPI_CHAR,
						 2, order_paragraphs[id], MPI_COMM_WORLD);
					}
				}
				// eliberez memoria
				for (int i = 0 ; i < nb_lines_paragraph[id]; i++) {
					free(comedy_paragraph[i]);
				}
				// reactualizez nb_lines_paragraph[id] si order_paragraphs[id]
				nb_lines_paragraph[id] = 0;
				order_paragraphs[id]++;
			}
			if (ok_start_paragraph[id] == 0 && 
				strcmp(line, "comedy\n") == 0) {
				//incepe paragraf nou de comedy
				ok_start_paragraph[id] = 1;
			}
			else if (ok_start_paragraph[id] == 1) {
				// linia curenta face parte dintr-un paragraf comedy si o retin
				comedy_paragraph[nb_lines_paragraph[id]] = malloc((strlen(line)+1)*sizeof(char));
				strcpy(comedy_paragraph[nb_lines_paragraph[id]], line);
				nb_lines_paragraph[id]++;
			}
		}
		else if (id == 2) {
			// FANTASY
			if (strcmp (line, "\n") == 0) {
				// s-a terminat un paragraf
				ok_start_paragraph[id] = 0;
				// daca paragrful era de fantasy, trebuie trimis
				if (nb_lines_paragraph[id] != 0) {
					nb_par_fantasy++;
					// trimit paragraful la worker-ul FANTASY - rank 3
					// mesajele vor avea ca tag numarul de ordine din fisier
					// (al catelea paragraf este in input)

					// intai trimit nr de linii al paragrafului
					MPI_Send(&nb_lines_paragraph[id], 1, MPI_INT, 3, order_paragraphs[id], MPI_COMM_WORLD);
					// paragraful
					for (int i = 0; i < nb_lines_paragraph[id]; i++) {
						// inaintea liniei trimit nr de caractere de pe linie
						int line_len = strlen(fantasy_paragraph[i]);
						MPI_Send(&line_len, 1, MPI_INT, 3, order_paragraphs[id], MPI_COMM_WORLD);
						//linia
						MPI_Send(fantasy_paragraph[i], line_len, MPI_CHAR,
						 3, order_paragraphs[id], MPI_COMM_WORLD);
					}
				}
				// eliberez memoria
				for (int i = 0 ; i < nb_lines_paragraph[id]; i++) {
					free(fantasy_paragraph[i]);
				}
				// actualizez nb_lines_paragraph[id] si order_paragraphs[id]
				nb_lines_paragraph[id] = 0;
				order_paragraphs[id]++;
			}
			if (ok_start_paragraph[id] == 0 && 
				strcmp(line, "fantasy\n") == 0) {
				// incepe paragraf nou fantasy
				ok_start_paragraph[id] = 1;
			}
			else if (ok_start_paragraph[id] == 1) {
				// linia citita face parte dintr-un paragraf fantasy si o retin
				fantasy_paragraph[nb_lines_paragraph[id]] = malloc((strlen(line)+1)*sizeof(char));
				strcpy(fantasy_paragraph[nb_lines_paragraph[id]], line);
				nb_lines_paragraph[id]++;		
			}
		}	
		else if (id == 3) {
			// SCIENCE-FICTION
			if (strcmp (line, "\n") == 0) {
				// s-a terminat un paragraf
				ok_start_paragraph[id] = 0;
				// daca era paragraf sf trebuie trimis
				if (nb_lines_paragraph[id] != 0) {
					nb_par_sf++;
					// trimit paragraful la worker-ul SCIENCE_FICTION - rank 4
					// mesajele vor avea ca tag numarul de ordine din fisier
					// (al catelea paragraf este in input)

					// intai trimit nr de linii al paragrafului
					MPI_Send(&nb_lines_paragraph[id], 1, MPI_INT, 4, order_paragraphs[id], MPI_COMM_WORLD);
					// paragraful
					for (int i = 0; i < nb_lines_paragraph[id]; i++) {
						// inainte trimit nr de caractere de pe linie
						int line_len = strlen(science_fiction_paragraph[i]);
						MPI_Send(&line_len, 1, MPI_INT, 4, order_paragraphs[id], MPI_COMM_WORLD);
						//linia
						MPI_Send(science_fiction_paragraph[i], line_len, MPI_CHAR,
						 4, order_paragraphs[id], MPI_COMM_WORLD);
					}
				}
				// eliberez memoria
				for (int i = 0 ; i < nb_lines_paragraph[id]; i++) {
					free(science_fiction_paragraph[i]);
				}
				// actualizez nb_lines_paragraph[id] si order_paragraphs[id]
				nb_lines_paragraph[id] = 0;
				order_paragraphs[id]++;
			}
			if (ok_start_paragraph[id] == 0 && 
				strcmp(line, "science-fiction\n") == 0) {
				// incepe paragraf nou SCIENCE-FICTION
				ok_start_paragraph[id] = 1;
			}
			else if (ok_start_paragraph[id] == 1) {
				// linia face parte dintr-un paragraf sf si o retin
				science_fiction_paragraph[nb_lines_paragraph[id]] = malloc((strlen(line)+1)*sizeof(char));
				strcpy(science_fiction_paragraph[nb_lines_paragraph[id]], line);
				nb_lines_paragraph[id]++;
			}
		}
	}
	// ramane netrimis ultimul paragraf din fisier
	if (nb_lines_paragraph[id] != 0) {
		if (id == 0) {
			nb_par_horror++;
			// trimit nr de linii al paragrafului
			MPI_Send(&nb_lines_paragraph[id], 1, MPI_INT, 1, order_paragraphs[id], MPI_COMM_WORLD);
			// paragraful
			for (int i = 0; i < nb_lines_paragraph[id]; i++) {
				// trimit nr de caractere de pe linie
				int line_len = strlen(horror_paragraph[i]);
				MPI_Send(&line_len, 1, MPI_INT, 1, order_paragraphs[id], MPI_COMM_WORLD);
				//linia
				MPI_Send(horror_paragraph[i], line_len, MPI_CHAR,
					 1, order_paragraphs[id], MPI_COMM_WORLD);
			}
		}
		if (id == 1) {
			nb_par_comedy++;
			// trimit nr de linii al paragrafului
			MPI_Send(&nb_lines_paragraph[id], 1, MPI_INT, 2, order_paragraphs[id], MPI_COMM_WORLD);
			// paragraful
			for (int i = 0; i < nb_lines_paragraph[id]; i++) {
				// trimit nr de caractere de pe linie
				int line_len = strlen(comedy_paragraph[i]);
				MPI_Send(&line_len, 1, MPI_INT, 2, order_paragraphs[id], MPI_COMM_WORLD);
				//linia
				MPI_Send(comedy_paragraph[i], line_len, MPI_CHAR,
					 2, order_paragraphs[id], MPI_COMM_WORLD);
			}
		}
		if (id == 2) {
			// trimit nr de linii al paragrafului
			nb_par_fantasy++;
			MPI_Send(&nb_lines_paragraph[id], 1, MPI_INT, 3, order_paragraphs[id], MPI_COMM_WORLD);
			// paragraful
			for (int i = 0; i < nb_lines_paragraph[id]; i++) {
				// trimit nr de caractere de pe linie
				int line_len = strlen(fantasy_paragraph[i]);
				MPI_Send(&line_len, 1, MPI_INT, 3, order_paragraphs[id], MPI_COMM_WORLD);
				//linia
				MPI_Send(fantasy_paragraph[i], line_len, MPI_CHAR,
					 3, order_paragraphs[id], MPI_COMM_WORLD);
			}
		}	
		if (id == 3) {
			nb_par_sf++;
			// trimit nr de linii al paragrafului
			MPI_Send(&nb_lines_paragraph[id], 1, MPI_INT, 4, order_paragraphs[id], MPI_COMM_WORLD);
			// paragraful
			for (int i = 0; i < nb_lines_paragraph[id]; i++) {
				// trimit nr de caractere de pe linie
				int line_len = strlen(science_fiction_paragraph[i]);
				MPI_Send(&line_len, 1, MPI_INT, 4, order_paragraphs[id], MPI_COMM_WORLD);
				//linia
				MPI_Send(science_fiction_paragraph[i], line_len, MPI_CHAR,
					 4, order_paragraphs[id], MPI_COMM_WORLD);
			}
		}	
	}
	fclose(fp);
	// trimit mesaj -100 <-> s a terminat citirea pt worker-ul respectiv
	int assigned_worker = id+1;
	int over = -100;
	MPI_Send(&over, 1, MPI_INT, assigned_worker, order_paragraphs[id], MPI_COMM_WORLD);
	pthread_exit(NULL);
}

// paragraful horror primit de thread-ul Reader din worker-ul cu rang 1
char **received_horror_paragraph;
// folosite pt gasirea intervalului pe care va procesa cele maxim 20 linii
// fiecare executing_thread deschis de readerThread
int interval_horror = -1;
pthread_mutex_t mutex_horror;

char **received_comedy_paragraph;
int interval_comedy = -1;
pthread_mutex_t mutex_comedy;

char **received_fantasy_paragraph;
int interval_fantasy = -1;
pthread_mutex_t mutex_fantasy;

char **received_sf_paragraph;
int interval_sf = -1;
pthread_mutex_t mutex_sf;

void *process_horror_paragraph(void *arg) {
	long size = (long)arg; // fiecare thread primeste dimensiunea paragrafului
	int start, end;
	while (interval_horror <= size/20 + 1) {
		pthread_mutex_lock(&mutex_horror);
		// regiune critica - fiecare thread gaseste pe ce interval modifica paragraful
		interval_horror ++;
		start = interval_horror * 20;
		end = min (((interval_horror+1) * 20), size);
		pthread_mutex_unlock(&mutex_horror);
		for (int i = start; i < end; i++) {
			int old_size = strlen(received_horror_paragraph[i]);
			received_horror_paragraph[i] = realloc(received_horror_paragraph[i],
				2*old_size*sizeof(char));
			strcpy(received_horror_paragraph[i], process_horror_line(received_horror_paragraph[i]));
		}
	}
	pthread_exit(NULL);
}

void *readerThread_function_horror(void *arg) {
	long id = (long)arg;
	int new_horror = 0;
	while (1) {
		// thread-ul Reader din worker-ul HORROR
		// va primi paragrafe horror, va porni thread-uri
		// pt a le procesa si le va trimite la master
		int order = -1;
		if (new_horror == 0) {
			MPI_Status status;
			MPI_Recv(&new_horror, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			order = status.MPI_TAG; // obtin tag-ul din status (= al catelea paragraf din fisier este)
		}
		if (new_horror == -100) {
			// mesajul ce semnifica ca am primit toate paragrafele horror din fisier
			break;
		}
		if (new_horror != 0) {
			// primesc un paragraf de new_horror linii si il retin
			received_horror_paragraph = malloc(new_horror * sizeof(char*));
			MPI_Status status;		
			for (int i = 0; i < new_horror; i++) {
				int length_line = 0;
				MPI_Recv(&length_line, 1, MPI_INT, 0, order, MPI_COMM_WORLD, &status);
				received_horror_paragraph[i] = malloc((length_line+1) * sizeof(char));
				MPI_Recv(received_horror_paragraph[i], length_line, MPI_CHAR, 0, order, MPI_COMM_WORLD, &status);
				received_horror_paragraph[i][length_line] = '\0';			
			}
			// thread-ul READER a primit paragraful HORROR de new_horror linii
			// va porni min(cores-1, new_horror/20+1) thread-uri
			// fiecare thread va procesa cate maxim 20 linii o data
			long cores = sysconf(_SC_NPROCESSORS_CONF);
			int NUM_THREADS = min(cores-1, new_horror/20+1);
			pthread_t executing_threads[NUM_THREADS];
			int r;
			long id_exec;
			void *status_pthread;
			for (id_exec = 0; id_exec < NUM_THREADS; id_exec++) {
				r = pthread_create(&executing_threads[id_exec], NULL, process_horror_paragraph, (void *)(long)new_horror);//id_exec
				if (r) {
					printf("Eroare la crearea thread-ului %ld\n", id_exec);
					exit(-1);
				}
			}
			for (id_exec = 0; id_exec < NUM_THREADS; id_exec++) {
				r = pthread_join(executing_threads[id_exec], &status_pthread);
				if (r) {
					printf("Eroare la asteptarea thread-ului %ld\n", id_exec);
					exit(-1);
				}
			}
			// thread-ul READER trimite la MASTER paragraful modificat
			// intai nr de linii pe care master sa-l astepte
			// apoi inainte de a trimite fiecare linie, trimite nr de caractere
			pthread_mutex_lock(&mutex_send);
			// regiune critica - va trimite maxim 1 thread un paragraf spre master
			MPI_Send(&new_horror, 1, MPI_INT, 0, order, MPI_COMM_WORLD);
			// paragraful
			for (int i = 0; i < new_horror; i++) {
				int line_len = strlen(received_horror_paragraph[i]);
				MPI_Send(&line_len, 1, MPI_INT, 0, order, MPI_COMM_WORLD);
				//linia
				MPI_Send(received_horror_paragraph[i], line_len, MPI_CHAR,
				 0, order, MPI_COMM_WORLD);
				//eliberez memoria
				free(received_horror_paragraph[i]);		
			}
			free(received_horror_paragraph);
			// astept paragraf nou		
			new_horror = 0;
			interval_horror = -1;
			pthread_mutex_unlock(&mutex_send);
		}
	}
	pthread_exit(NULL);
}

void *process_comedy_paragraph(void *arg) {
	long size = (long)arg; // fiecare thread primeste nr de linii al paragrafului
	int start, end;
	while (interval_comedy < size/20 + 1) {
		pthread_mutex_lock(&mutex_comedy);
		// regiune critica - fiecare thread gaseste pe ce sub interval modifica paragraful
		interval_comedy++;
		start = interval_comedy * 20;
		end = min (((interval_comedy+1) * 20), size);
		pthread_mutex_unlock(&mutex_comedy);
		if (interval_comedy <= size/20+1) {
			for (int i = start; i < end; i++) {
				int old_size = strlen(received_comedy_paragraph[i]);
				strcpy(received_comedy_paragraph[i], process_comedy_line(received_comedy_paragraph[i]));
			}
		}
	}
	pthread_exit(NULL);
}

void *readerThread_function_comedy(void *arg) {
	long id = (long)arg;
	int new_comedy = 0;
	while (1) {
		int order = -1;
		if (new_comedy == 0) {
			MPI_Status status;
			MPI_Recv(&new_comedy, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			order = status.MPI_TAG; // ordinea paragrafului in fisier
		}
		if (new_comedy == -100) {
			// mesaj ce semnifica ca nu mai sunt paragrafe COMEDY
			break;
		}
		if (new_comedy != 0) {
			// o sa primesc un paragraf comedy de new_comedy linii
			received_comedy_paragraph = malloc(new_comedy * sizeof(char*));
			MPI_Status status;
			for (int i = 0; i < new_comedy; i++) {
				int length_line = 0;
				MPI_Recv(&length_line, 1, MPI_INT, 0, order, MPI_COMM_WORLD, &status);
				received_comedy_paragraph[i] = malloc((length_line+1) * sizeof(char));
				MPI_Recv(received_comedy_paragraph[i], length_line, MPI_CHAR, 0, order, MPI_COMM_WORLD, &status);
				received_comedy_paragraph[i][length_line] = '\0';
			}
			// thread-ul READER a primit paragraful COMEDY de new_comedy linii
			// va porni min(cores-1, new_comedy/20+1) thread-uri
			// fiecare thread va procesa cate maxim 20 linii o data
			long cores = sysconf(_SC_NPROCESSORS_CONF);
			int NUM_THREADS = min(cores-1, new_comedy/20+1);
			pthread_t executing_threads[NUM_THREADS];
			int r;
			long id_exec;
			void *status_pthread;
			for (id_exec = 0; id_exec < NUM_THREADS; id_exec++) {
				r = pthread_create(&executing_threads[id_exec], NULL, process_comedy_paragraph, (void *)(long)new_comedy);
				if (r) {
					printf("Eroare la crearea thread-ului %ld\n", id_exec);
					exit(-1);
				}
			}
			for (id_exec = 0; id_exec < NUM_THREADS; id_exec++) {
				r = pthread_join(executing_threads[id_exec], &status_pthread);
				if (r) {
					printf("Eroare la asteptarea thread-ului %ld\n", id_exec);
					exit(-1);
				}
			}
			// thread-ul READER trimite la MASTER paragraful modificat
			// prima data trimite nr de linii pe care master o sa il astepte
			// apoi inainte de fiecare linie va trimite nr de caractere 
			pthread_mutex_lock(&mutex_send);
			// regiune critica - maxim 1 thread va trimite mesaje spre master o data
			MPI_Send(&new_comedy, 1, MPI_INT, 0, order, MPI_COMM_WORLD);
			// paragraful
			for (int i = 0; i < new_comedy; i++) {
				int line_len = strlen(received_comedy_paragraph[i]);
				MPI_Send(&line_len, 1, MPI_INT, 0, order, MPI_COMM_WORLD);
				//linia
				MPI_Send(received_comedy_paragraph[i], line_len, MPI_CHAR,
				 0, order, MPI_COMM_WORLD);
				//eliberez memoria
				free(received_comedy_paragraph[i]);		
			}
			free(received_comedy_paragraph);
			// pot sa astept paragraf COMEDY nou		
			new_comedy = 0;
			interval_comedy = -1;
			pthread_mutex_unlock(&mutex_send);		
		}
	}
	pthread_exit(NULL);
}

void *process_fantasy_paragraph(void *arg) {
	long size = (long) arg; // fiecare thread are nr de linii din paragraf
	int start, end;
	while (interval_fantasy <= size/20 + 1) {
		pthread_mutex_lock(&mutex_fantasy);
		// regiune critica - fiecare thread gaseste pe ce sub interval modifica paragraful
		interval_fantasy++;
		start = interval_fantasy * 20;
		end = min (((interval_fantasy+1) * 20), size);
		pthread_mutex_unlock(&mutex_fantasy);
		for (int i = start; i < end; i++) {
			int old_size = strlen(received_fantasy_paragraph[i]);
			strcpy(received_fantasy_paragraph[i], process_fantasy_line(received_fantasy_paragraph[i]));
		}
	}
	pthread_exit(NULL);
}

void *readerThread_function_fantasy(void *arg) {
	long id = (long)arg;
	int new_fantasy = 0;
	while (1) {
		int order = -1;
		if (new_fantasy == 0) {
			MPI_Status status;
			MPI_Recv(&new_fantasy, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			order = status.MPI_TAG; // al catelea paragraf este in fisier
		}
		if (new_fantasy == -100) {
			// nu mai sunt paragrafe FANTASY de primit
			break;
		}
		if (new_fantasy != 0) {
			// o sa primesc paragraf FANTASY de new_fantasy linii
			received_fantasy_paragraph = malloc(new_fantasy * sizeof(char*));
			MPI_Status status;
			for (int i = 0; i < new_fantasy; i++) {
				int length_line = 0;
				MPI_Recv(&length_line, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
				received_fantasy_paragraph[i] = malloc((length_line+1) * sizeof(char));
				MPI_Recv(received_fantasy_paragraph[i], length_line, MPI_CHAR, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
				received_fantasy_paragraph[i][length_line] = '\0';
			}
			// thread-ul READER a primit paragraful FANTASY de new_fantasy linii
			// va porni min(cores-1, new_fantasy/20+1) thread-uri
			// fiecare thread va procesa cate maxim 20 linii o data
			long cores = sysconf(_SC_NPROCESSORS_CONF);
			int NUM_THREADS = min(cores-1, new_fantasy/20+1);
			pthread_t executing_threads[NUM_THREADS];
			int r;
			long id_exec;
			void *status_pthread;
			for (id_exec = 0; id_exec < NUM_THREADS; id_exec++) {
				r = pthread_create(&executing_threads[id_exec], NULL, process_fantasy_paragraph, (void *)(long)new_fantasy);//id_exec
				if (r) {
					printf("Eroare la crearea thread-ului %ld\n", id_exec);
					exit(-1);
				}
			}
			for (id_exec = 0; id_exec < NUM_THREADS; id_exec++) {
				r = pthread_join(executing_threads[id_exec], &status_pthread);
				if (r) {
					printf("Eroare la asteptarea thread-ului %ld\n", id_exec);
					exit(-1);
				}
			}
			// thread-ul READER trimite la MASTER paragraful modificat
			// prima data trimite nr de linii pe care master o sa il astepte
			// apoi inainte de fiecare linie va trimite nr de caractere 
			pthread_mutex_lock(&mutex_send);
			// regiune critica - maxim 1 thread va trimite mesaje spre master o data
			MPI_Send(&new_fantasy, 1, MPI_INT, 0, order, MPI_COMM_WORLD);
			// paragraful
			for (int i = 0; i < new_fantasy; i++) {
				int line_len = strlen(received_fantasy_paragraph[i]);
				MPI_Send(&line_len, 1, MPI_INT, 0, order, MPI_COMM_WORLD);
				//linia
				MPI_Send(received_fantasy_paragraph[i], line_len, MPI_CHAR,
				 0, order, MPI_COMM_WORLD);
				//eliberez memoria
				free(received_fantasy_paragraph[i]);		
			}
			free(received_fantasy_paragraph);
			// pot sa primesc paragraf nou		
			new_fantasy = 0;
			interval_fantasy = -1;
			pthread_mutex_unlock(&mutex_send);
		}
	}
	pthread_exit(NULL);
}

void *process_sf_paragraph(void *arg) {
	long size = (long) arg; // fiecare thread va stii nr de linii din paragraf
	int start, end;
	while (interval_sf < size/20 + 1) {
		pthread_mutex_lock(&mutex_sf);
		// regiune critica - fiecare thread gaseste pe ce sub interval modifica paragraful
		interval_sf++;
		start = interval_sf * 20;
		end = min (((interval_sf + 1) * 20), size);
		pthread_mutex_unlock(&mutex_sf);
		if (interval_sf <= size/20 + 1) {
			for (int i = start; i < end; i++) {
				int old_size = strlen(received_sf_paragraph[i]);
				strcpy(received_sf_paragraph[i], process_sf_line(received_sf_paragraph[i]));
			}
		}
	}
	pthread_exit(NULL);
}

void *readerThread_function_sf(void *arg) {
	long id = (long)arg;
	int new_sf = 0;
	while (1) {
		int order = -1;
		if (new_sf == 0) {
			MPI_Status status;
			MPI_Recv(&new_sf, 1, MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			order = status.MPI_TAG; // al catelea paragraf este in fisier
		}
		if (new_sf == -100) {
			// mesaj de oprire - nu mai sunt paragrafe sf
			break;
		}
		if (new_sf != 0) {
			// am primit paragraf SF de new_sf linii
			received_sf_paragraph = malloc(new_sf * sizeof(char*));
			MPI_Status status;
			// paragraful	
			for (int i = 0; i < new_sf; i++) {
				int length_line = 0;
				MPI_Recv(&length_line, 1, MPI_INT, 0, order, MPI_COMM_WORLD, &status);
				received_sf_paragraph[i] = malloc((length_line+1) * sizeof(char));
				MPI_Recv(received_sf_paragraph[i], length_line, MPI_CHAR, 0, order, MPI_COMM_WORLD, &status);
				received_sf_paragraph[i][length_line] = '\0';
			}
			// thread-ul READER a primit paragraful SCIENCE-FICTION de new_sf linii
			// va porni min(cores-1, new_fantasy/20+1) thread-uri
			// fiecare thread va procesa cate maxim 20 linii o data
			long cores = sysconf(_SC_NPROCESSORS_CONF);
			int NUM_THREADS = min(cores-1, new_sf/20+1);
			pthread_t executing_threads[NUM_THREADS];
			int r;
			long id_exec;
			void *status_pthread;
			for (id_exec = 0; id_exec < NUM_THREADS; id_exec++) {
				r = pthread_create(&executing_threads[id_exec], NULL, process_sf_paragraph, (void *)(long)new_sf);
				if (r) {
					printf("Eroare la crearea thread-ului %ld\n", id_exec);
					exit(-1);
				}
			}
			for (id_exec = 0; id_exec < NUM_THREADS; id_exec++) {
				r = pthread_join(executing_threads[id_exec], &status_pthread);
				if (r) {
					printf("Eroare la asteptarea thread-ului %ld\n", id_exec);
					exit(-1);
				}
			}
			// thread-ul READER trimite la MASTER paragraful modificat
			// prima data trimite nr de linii pe care master o sa il astepte
			// apoi inainte de fiecare linie va trimite nr de caractere 
			pthread_mutex_lock(&mutex_send);
			// regiune critica - maxim 1 thread va trimite mesaje spre master o data
			MPI_Send(&new_sf, 1, MPI_INT, 0, order, MPI_COMM_WORLD);
			// paragraful
			for (int i = 0; i < new_sf; i++) {
				int line_len = strlen(received_sf_paragraph[i]);
				MPI_Send(&line_len, 1, MPI_INT, 0, order, MPI_COMM_WORLD);
				//linia
				MPI_Send(received_sf_paragraph[i], line_len, MPI_CHAR,
				 0, order, MPI_COMM_WORLD);
				//eliberez memoria
				free(received_sf_paragraph[i]);		
			}
			free(received_sf_paragraph);
			// se poate primi paragraf science-fiction nou	
			new_sf = 0;
			interval_sf = -1;
			pthread_mutex_unlock(&mutex_send);
		}
	}
	pthread_exit(NULL);
}

int main (int argc, char *argv[])
{
	if (argc < 2) {
		printf("Not enough arguments\n");
	}

	horror_paragraph = malloc(1000000*sizeof(char*));
	comedy_paragraph = malloc(1000000*sizeof(char*));
	fantasy_paragraph = malloc(1000000*sizeof(char*));
	science_fiction_paragraph = malloc(1000000*sizeof(char*));

	int  numtasks, rank;

	int provided;
	MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
	MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
	MPI_Comm_rank(MPI_COMM_WORLD,&rank);
	int recv_num;

	if (rank == 0) {
		input = malloc ((strlen(argv[1])+5)*sizeof(char));
		strcpy(input, argv[1]);
		pthread_t threads[4];
		int r;
		long id;
		void *status;
		// thread-urile pt citirea in paralel a fisierului
		for (id = 0; id < 4; id++) {
			r = pthread_create(&threads[id], NULL, f_read_file, (void *)id);
			if (r) {
				printf("Eroare la crearea thread-ului %ld\n", id);
				exit(-1);
			}
		}
		for (id = 0; id < 4; id++) {
			r = pthread_join(threads[id], &status);
			if (r) {
				printf("Eroare la asteptarea thread-ului %ld\n", id);
				exit(-1);
			}
		}
		// numarul de paragrafe asteptate de MASTER
		int total_par = nb_par_horror + nb_par_comedy + nb_par_fantasy + nb_par_sf;
		// primesc paragraf cu paragraaf textul modificat pe care
		// il salvez in vectorul de structuri
		struct Paragraph *paragraphs = malloc(total_par * sizeof(struct Paragraph));
		for (int p = 0; p < total_par; p++) {
			int nb_lines = 0;
			MPI_Status status_recv;
			MPI_Recv(&nb_lines, 1, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status_recv);	
			int order = status_recv.MPI_TAG; // voi stii al catelea paragraf fusese in fisierul de intrare
			int type_source = status_recv.MPI_SOURCE;  // tipul paragrafului - in functie de unde a venit
			paragraphs[p].type = status_recv.MPI_SOURCE;
			paragraphs[p].order = status_recv.MPI_TAG;
			paragraphs[p].nb_lines = nb_lines;
			paragraphs[p].text = malloc (nb_lines * sizeof(char*));
			// paragraful
			for (int i = 0; i < nb_lines; i++) {
				int length_line = 0;
				MPI_Recv(&length_line, 1, MPI_INT, type_source, order, MPI_COMM_WORLD, &status_recv);
				paragraphs[p].text[i] = malloc((length_line+1) * sizeof(char));
				MPI_Recv(paragraphs[p].text[i], length_line, MPI_CHAR, type_source, order, MPI_COMM_WORLD, &status_recv);
				paragraphs[p].text[i][length_line] = '\0';
			}
		}
		// refac ordinea paragrafelor - le sortez crescator
		qsort((void *)paragraphs, total_par, sizeof(paragraphs[0]), compare_function);
		// fisieul de output obtinut din numele celui primit ca input
		input[strlen(input)-2] = 'u';
		input[strlen(input)-3] = 'o';
		FILE *ptr = fopen(input, "w");
		for (int p = 0; p < total_par; p++) {
			if (paragraphs[p].type == 1) {
				fprintf(ptr, "horror\n");
			} else if (paragraphs[p].type == 2) {
				fprintf(ptr, "comedy\n");
			} else if (paragraphs[p].type == 3) {
				fprintf(ptr, "fantasy\n");
			} else if (paragraphs[p].type == 4) {
				fprintf(ptr, "science-fiction\n");
			}
			for (int i = 0 ; i < paragraphs[p].nb_lines; i++) {
				fprintf(ptr, "%s", paragraphs[p].text[i]);
			}
			if (p != total_par-1) {
				fprintf(ptr, "\n");
			}
		}
		fclose (ptr);

	}
	else if (rank == 1) {
		// nodul worker HORROR
		// 1 thread reader - face receive si porneste cele
		// P-1 thread-uri de executie care proceseaza maxim 20 linii o data
		// P thread-uri disponibile pe sistem
		pthread_t reader;
		int r;
		long id = 0;
		void *status;
		r = pthread_create(&reader, NULL, readerThread_function_horror, (void *)id);
		if (r) {
			printf("Eroare la crearea thread-ului %ld\n", id);
			exit(-1);
		}			
		r = pthread_join(reader, &status);
		if (r) {
			printf("Eroare la asteptarea thread-ului %ld\n", id);
			exit(-1);
		}
	}
	else if (rank == 2) {
		// nodul worker COMEDY
		pthread_t reader;
		int r;
		long id = 0;
		void *status;
		// porneste thread-ul reader
		r = pthread_create(&reader, NULL, readerThread_function_comedy, (void *)id);
		if (r) {
			printf("Eroare la crearea thread-ului %ld\n", id);
			exit(-1);
		}	
		r = pthread_join(reader, &status);
		if (r) {
			printf("Eroare la asteptarea thread-ului %ld\n", id);
			exit(-1);
		}
	}
	else if (rank == 3) {
		// nodul worker FANTASY
		// porneste thread-ul reader
		pthread_t reader;
		int r;
		long id = 0;
		void *status;
		r = pthread_create(&reader, NULL, readerThread_function_fantasy, (void *)id);
		if (r) {
			printf("Eroare la crearea thread-ului %ld\n", id);
			exit(-1);
		}			
		r = pthread_join(reader, &status);
		if (r) {
			printf("Eroare la asteptarea thread-ului %ld\n", id);
			exit(-1);
		}	
	}
	else if (rank == 4) {
		// nodul worker SCIENCE-FICTION
		// porneste thread-ul reader
		pthread_t reader;
		int r;
		long id = 0;
		void *status;
		r = pthread_create(&reader, NULL, readerThread_function_sf, (void *)id);
		if (r) {
			printf("Eroare la crearea thread-ului %ld\n", id);
			exit(-1);
		}			
		r = pthread_join(reader, &status);
		if (r) {
			printf("Eroare la asteptarea thread-ului %ld\n", id);
			exit(-1);
		}	
	}
	MPI_Finalize();
}
