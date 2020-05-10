[Source](http://www.iac.rm.cnr.it/~massimo/PONE2014_15.html "Permalink to Progetto per chi intende sostenere Sistemi Operativi II e Laboratorio II")

# Progetto per chi intende sostenere Sistemi Operativi II e Laboratorio II

### Progetto per l'esame di Programmazione di Sistema A.A. 2014/2015

##### (bozza aggiornata al 16 gennaio 2015) 
**Scopo:**  

Sviluppo di un semplice file system distribuito. Il file system ha l'equivalente delle primitive *open, close, read* e _write_.  
I dati dei file sono mantenuti in una copia primaria da parte del server ed in copie secondarie da parte dei client.   
La coerenza mantenuta attraverso un meccanismo di invalidazione (attivato dal server) delle cache dei client.   
Il server deve supportare sia la modalità multi-processo che multi-thread selezionabile da file di configurazione.  
Nel seguito xxx.yyy.www.zzz sta ad indicare un indirizzo IPv4 nella forma "dotted decimal" (_e.g._, 150.146.2.201) ma l'indirizzo   
del server deve poter essere specificato anche con il nome mnemonico (_e.g.,_ twin.iac.rm.cnr.it).  
  
Caratteristiche:

* Il server gestisce in maniera concorrente le richieste dei client__
* Il server in ascolto, per _default,_ sulla porta TCP **6020**. La porta pu essere modificata da file di configurazione o da linea comandi tramite l'opzione **-P**
* Il client utilizza una libreria (_libmydfs.a)_ che supporta le seguenti quattro primitive:
    * MyDFSId *mydfs_open(xxx.yyy.www.zzz, char ***nomefile**, int **modo**, int ***err**)
    * int mydfs_close(**MyDFSId**)
    * int mydfs_read(**MyDFSId**, int **pos**, void ***ptr**, unsigned int **size**)
    * int mydfs_write(**MyDFSId**, int **pos**, void ***ptr**, unsigned int **size**)
        MyDFSId un identificatore del file. Si pu assumere una definizione del tipo   
        typedef struct mydfsid { xxx; yyy; zzz; } **MyDFSId**;  
        dove per il numero di elementi della struttura (ed il loro tipo) non esistono vincoli   
        (ovviamente la struttura deve avere almeno un elemento).  
      Per quanto riguarda il nome del file (parametro **nomefile**) deve essere un path specificato rispetto ad una directory di base predefinita nel    
      file di configurazione (nello stile Unix, con separatore **/**).     
      Per quanto riguarda il **modo**, devono essere supportate le seguenti modalit (per il significato vedere la documentazione sulla _open_ Unix):  

    *  O_RDONLY, O_WRONLY, O_RDWR, O_CREAT, O_TRUNC, O_EXCL,  O_EXLOCK 
     per _default_ i file sono condivisi in lettura.  
     I parametri ptr e size hanno lo stesso significato della _read_ Unix.  
      Il parametro pos indica la posizione rispetto cui effettuare l'operazione di lettura o scrittura.   
     Pu assumere i valori SEEK_SET, SEEK_CUR, o SEEK_END con gli stessi significati che hanno per la _fseek.  

    * Il valore di ritorno della mydfs_open :
        * un puntatore ad un MyDFSId se l'operazione andata a buon fine;
        * NULL in caso di errore. In questo caso la locazione puntata da **err** conterrà
            * -1 in caso il file aperto in scrittura da un altro client;
            * -2 in caso il server non sia raggiungibile;
            * -3 in caso di errore sul file (ad esempio file aperto in sola lettura che non esiste);
    * Il valore di ritorno della mydfs_close :
        * 0 se l'operazione andata a buon fine;
        * -1 in caso di errore.
    * Il valore di ritorno della mydfs_read :
        * 0 se il file stato completamente letto;
        * -1 in caso di errore;
        * \>0 ed uguale al numero di byte effettivamente letti.
    * Il valore di ritorno della mydfs_write :
        * -1 in caso di errore;
        * \>= 0 ed uguale al numero di byte effettivamente scritti.
* La libreria implementa un meccanismo di _caching _sia in lettura, sia in scrittura.   

    * In lettura il client accede i dati dalla propria cache, se disponibili. Altrimenti richiede al server i dati necessari. Ogni richiesta viene effettuata in unit di un blocco logico la cui dimensione 65536 byte. Se il file pi piccolo oppure se l'ultimo frammento del file pi piccolo viene ritornato il numero di byte effettivamente disponbile.
        * Per mantenere la coerenza, **prima **di ogni operazione di lettura dalla cache il client deve controllare se ha ricevuto dal server un messaggio di invalidazione della cache (vedi punto successivo).
        * Risulta quindi necessario che ogni client abbia una connessione di _controllo_ su cui ricevere i messaggi di invalidazione.  

    * In scrittura, le operazioni sono effettuate sulla cache locale e sono inviate al server quando viene invocata la _mydfs_close._
* Quando un file viene aperto in scrittura, viene assegnato un lock al client che ha invocato la mydfs_open. Se il file gi aperto in scrittura da parte di un altro client viene ritornato un errore specifico (vedi descrizione della mydfs_open). Se un file viene aperto in scrittura tutte le copie tenute in cache dai client vengono invalidate. L'invalidazione avviene tramite un messaggio inviato dal server ai client che hanno una copia del file in cache. A questo scopo il server mantiene una tabella che contiene una riga per ogni file aperto ed in  cui sono elencati tutti i client che hanno il file aperto in lettura.   
* Per evitare che un client che abbia aperto il file in scrittura lo tenga bloccato anche se termina in maniera anomale (senza effettuare la mydfs_close), deve essere implementato un meccanismo di _heart-beating: _periodicamente (ad esempio ogni minuto) il server contatta i client che tengono file aperti in scrittura inviando un messaggio sulla connessione di controllo. Se il client non risponde entro un certo intervallo di tempo, il server assume che il client non sia pi attivo e riprende il lock sul file.  
* I file possono essere creati tramite la mydfs_open oppure essere pre-esistenti.   
* Il server ed il client devono offrire le stesse funzionalit sotto Linux e Windows ed essere interoperabili (server Linux, client Windows oppure server Windows, client Linux oltre a, ovviamente, configurazioni solo Linux o solo Windows)  

    * la scelta della modalit (multi-thread o multi-processo) del server deve poter essere effettuata all'atto della  
partenze (tramite opzioni e/o lettura di un file di configurazione).
    * le scelte sul numero di thread o processi e sulla porta su cui rimane in attesa il server devono essere modificabili tramite opzioni e/o lettura di un file di configurazione.
* Il file system non fa distinzione tra file binari o di testo.  
* Il server deve funzionare in modalit daemon sotto Unix/Linux.

Suggerimenti:

* Definire un semplice protocollo per lo scambio delle informazioni tra client e server (comando-risposta);
* Utilizzare una connessione distinta per i dati e una per il controllo;  
* Per il meccanismo di caching utilizzare i concetti di file mapping ( un suggerimento non vincolante);
* Per il meccanismo di _heart-beating _usare un thread separato oppure un meccanismo tipo _signal;_  
* client e server **devono** essere provati NON solo in locale (sullo stesso sistema) ma anche su sistemi distinti.  

Il progetto può essere realizzato in gruppi di **due** persone.  

Al progetto andrà allegata una breve (5-10 pagine) relazione che descriva (e motivi) le scelte progettuali. 

  
