# DistributedTextProcesor

./process file.in

Folosesc 5 noduri MPI, Master fiind cel de rang 0, iar workerii de horror,
comedy, fantasy, science-fiction sunt, in ordine, nodurile de rang 1, 2, 3, 4.
Master-ul porneste 4 thread-uri de citire ce vor executa functia f_read_file,
fiecare thread cautand de fapt paragrafele corespunzatoare: thread 0 - horror,
thread 1 - comedy, thread 2 - fantasy, thread 3 - science-fiction.

Citirea paralela - functia f_read_file:
Fiecare thread deschide fisierul si citeste linie cu linie. Fiecare thread
va retine doar paragraful corespunzator (cate unul pe rand) si atunci cand
paragraful se termina ("\n\n") va fi trimis spre worker.
Folosesc variabilele globale:
- vectorul ok_start_paragraph[], unde ok_start_paragraph[i]= 1 inseamna ca a
threadul i a inceput un pragraf nou
- vectorul nb_lines_paragraph[], unde fiecare thread i retine cate linii are
paragraful curent
- vectorul order_paragraphs[] unde threadul i va stii (din order_paragraphs[i])
al catelea paragraf este cel curent in fisier
Pentru thread-ul 0 (HORROR): Daca linia curenta este newline, inseamna ca tocmai
s-a terminat un paragraf si daca era paragraf horror (adica
nb_lines_paragraph[0] > 0) il va trimite catre worker-ul horror - cu rangul 1:
intai numarul de linii, dupa va incepe sa trimita linie cu linie paragraful
dar cu lungimea fiecarei liniei inainte (pentru alocarea de memorie in receive).
Mesajele vor fi trimise cu tag-ul order_paragraph[0] pe care il vor pastra
pana la scrierea in fiserul de output.
Daca linia curenta nu este newline, inseamna ca ori incepe pargraf nou, ori
deja incepuse paragraf: daca linia este "horror", inseamna ca incepe paragraf
horror (care ma intereseaza) si actualizez ok_start_paragraph[0] = 1 si
numarul de paragrafe. Daca ok_start_paragraph[0] == 1 inseamna ca citesc un 
pragraf care ma intereseaza si trbuie retinut in  horror_paragraph.
Asemanator pentru restul thread-urilor, dar fiecare va cauta sa retina si sa
trimita paragraful corespunzator.
Dupa ce am iesit din while, mai verific daca au ramas paragrafe netrimise (pt ca
trimit doar atunci cand linia este newline si dupa ultimul paragraf din fisier 
nu mai sunt \n\n) si thread-ul corespunzator trimite si acel paragraf.
Fiecare thread va trimite apoi un intreg (-100) ce va semnifica in thread-urile
care fac receive ca s-a terminat citirea.

Primirea paragrafelor:
Fiecare worker va deschide cate 1 thread Reader care va face Receive si va
deschide apoi alte thread-uri pentru a procesa paragraful.
Functiile rulate de thread-urile care fac receive sunt readerThread_function_X,
unde X = {horror, comedy, fantasy, science-fiction}. In aceste functii,
thread-ul Reader va primi paragrafele corespunzatoare in while(1) astfel:
se va primi prima data un intreg in variabila new_X de la sursa 0 si se 
accepta orice tag (pe care il retine); apoi se primesc new_X linii, cu
lungimea fiecarei linii inaintea liniei (pentru alocarea de memorie). Primirea
de date se va opri cand se primeste in new_X valoarea -100.

Procesarea pargrafelor:
Dupa ce paragraful a fost primit, se vor porni thread-urile ce vor executa
procesarea: minimul dintre numarul de thread-uri disponibile - 1 si numarul
de thread-uri necesare pt a procesa new_X linii (adica new_X/20+1) - iar
fiecare thread va procesa maxim 20 linii o data. Aceste threaduri vor executa
functia process_X_paragraph si argumentul functiei va fi de fapt
numarul total de linii al pargarfului.
Fiecare thread va putea procesa maxim 20 linii o data, intre start si end, cu
start si end calculati in functie de interval_X: initial fecare thread vede
interval_X ca fiind -1 si va intra in while; aici, toate vor incerca sa intre
in regiunea critica pentru a-si stabili intervalul propriu; mutexul va permite
un singur thread pe rand si astefel variabila interval_X se va modifica corect.
Dupa ce un thread a putut sa stabileasca start si end, va apela functia
process_X_line pe toate liniile lui pentru a procesa corect fiecare linie
in functie de tipul de paragraf din care face parte.

Trimiterea spre master a pargrafelor:
Dupa ce paragraful curent a fost procesat(thread-ul Reader a dat join la
executing_threads), thread-ul Reader va trimite paragraful inapoi spre master:
va trimite intai numarul de linii, apoi linie cu linie paragraful (toate
mesajele pe tag-ul order cu care au venit) cu tag-ul cu care au fost primite.
Folosesc mutex-ul mutex_send pentru ca se vor rula in paralel 4 thread-uri
Reader (unul din fiecare worker si e posibil sa vrea toate 4 sa trimita in
acelasi timp mesaje spre acelasi master).

Primirea in master si scrierea in fisier:
Dupa ce s-a terminat citirea in paralel, master-ul va stii numarul total de
paragrafe pe care il astepta.
Master-ul va primi intai toate paragrafele, le retine si apoi va scrie in fisier
Folosesc vectorul de structuri Paragraph *paragraphs, ce retine pt fiecare
intrare: type (1=horror, 2 = comedy, 3 = fantasy, 4 = sf) - luat din sursa
mesajului primit; order - luat din tag-ul mesajului primit, nb_lines- nr de
linii al unui paragraf (va fi trimis mereu inainte paragrafului); text** -
paragraful.
Dupa ce am terminat de primit numarul asteptat de paragrafe, voi sorta vectorul
de structuri crescator dupa order (refac ordinea initiala) si voi putea scrie
in fisierul de output (file.out).
