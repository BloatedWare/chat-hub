# ChatHub
Crée par Anas Rifak et Abdelhamid Saidi
Serveur de chat multi-clients écrit en **C**, au-dessus de **TCP/IPv4** (sockets BSD).
Il prend en charge les messages publics, les messages privés, des pseudos uniques,
et notifie tout le monde des arrivées et départs.

Le projet est séparé en deux exécutables indépendants : le **serveur** (`src/server.c`)
et le **client** (`src/client.c`).

---

## 1. Dépendances

| Dépendance | Détail |
|------------|--------|
| Compilateur C | `gcc` (ou `clang`), norme C99 ou supérieure |
| Threads | **POSIX threads** (`pthread`) — inclus dans la glibc moderne |
| Système | POSIX : Linux / WSL / macOS (utilise `<sys/socket.h>`, `<arpa/inet.h>`) |

Aucune bibliothèque externe n'est nécessaire.

---

## 2. Compilation

```bash
mkdir -p bin/
gcc -Wall -Wextra -pthread src/server.c -o bin/server
gcc -Wall -Wextra -pthread src/client.c -o bin/client
```

- `-pthread` active le support des threads à la compilation **et** à l'édition de liens.
- `-Wall -Wextra` est recommandé : le code compile sans avertissement.

> Sur les anciennes glibc, remplacer `-pthread` par `-lpthread` en fin de ligne si besoin.

---

## 3. Exécution

L'ordre est important : **lancer d'abord le serveur, puis les clients.**

### 3.1 Serveur

```bash
bin/server <port> [adresse-ip]
```

| Argument | Rôle |
|----------|------|
| `<port>` | Port d'écoute (ex. `8080`) |
| `[adresse-ip]` | Interface d'écoute (optionnel, défaut `0.0.0.0` = toutes les interfaces) |

Exemple :

```bash
bin/server 8080
```

### 3.2 Client

```bash
bin/client <port> <adresse-ip>
```

Exemple (serveur sur la même machine) :

```bash
bin/client 8080 127.0.0.1
```

On peut lancer autant de clients que voulu, depuis plusieurs terminaux ou plusieurs machines.

### 3.3 Commandes disponibles dans le client

| Commande | Effet |
|----------|-------|
| `<texte>` | Envoie un message public à tous les autres clients |
| `/pseudo <nom>` | Change votre pseudo (doit être **unique** et **sans espaces**) |
| `/msg <pseudo> <message>` | Envoie un message **privé** à un client précis |
| `/exit` | Quitte la session proprement |

Tant qu'aucun `/pseudo` n'a été choisi, un pseudo par défaut `default-pseudo[N]` est attribué.

---

## 4. Protocole de communication

Protocole **texte**, simple et sans en-tête binaire, au-dessus d'une connexion **TCP** fiable.

- **Transport :** TCP/IPv4, `socket(AF_INET, SOCK_STREAM, 0)`.
- **Encodage :** texte brut (ASCII/UTF-8). Pas de longueur préfixée ni de délimiteur :
  on s'appuie sur le flux TCP. Le récepteur termine systématiquement par `'\0'`
  l'octet suivant ce qu'il a lu (`buffer[bytes_read] = '\0'`).
- **Taille d'un message :** côté client, un message saisi doit vérifier
  `1 ≤ longueur ≤ 1023` octets (`SEND_BUFF_SZ - 1`).
- **Sens client → serveur :** le client envoie la ligne saisie telle quelle.
  Le **serveur** interprète les préfixes de commande (`/pseudo `, `/msg `, `/exit`).
  Une ligne sans préfixe est un message public.
- **Sens serveur → client :** le client **affiche tel quel** tout ce qu'il reçoit.
  Le serveur met donc en forme les lignes avant de les envoyer.

### Messages émis par le serveur

| Type | Format |
|------|--------|
| Message public | `pseudo: message` |
| Message privé (au destinataire) | `[private] expéditeur: message` |
| Accusé (à l'expéditeur) | `[private -> destinataire] message` |
| Arrivée | `*** X has joined the chat ***` |
| Départ | `*** X has left the chat ***` |
| Renommage | `*** X is now known as Y ***` |
| Erreurs / aide | `*** That pseudo is already taken. Pick another. ***`, `*** No user named 'x' ***`, `*** Usage: /msg <pseudo> <message> ***`, etc. |

Le détail des échanges (diagrammes de séquence) est dans [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

---

## 5. Choix techniques majeurs

- **Un thread détaché par client (serveur).** À chaque `accept`, le serveur crée un
  thread `client_routine` puis appelle `pthread_detach` : le thread libère
  automatiquement ses ressources à sa fin (pas de fuite, pas de `pthread_join`).
- **Liste chaînée de clients + un seul mutex.** Tous les clients sont stockés dans
  une liste chaînée globale. **Un unique mutex** (`client_list_lock`) protège à la fois
  le parcours et la modification de la liste *et* la diffusion des messages. Une seule
  fonction de diffusion (`broadcast`) est réutilisée par les messages publics et par
  les annonces système, ce qui garde la logique de verrouillage à un seul endroit.
- **`accept` avant insertion.** Le client n'est ajouté à la liste qu'une fois la
  connexion acceptée et son descripteur (`sd`) renseigné, pour qu'aucun autre thread
  ne voie un client à moitié initialisé.
- **Client à deux threads.** Réception (affichage) et émission (lecture de `stdin`)
  tournent en parallèle, pour pouvoir recevoir pendant qu'on tape.
- **Pseudos sans espaces.** Contrainte volontaire : elle rend l'analyse de
  `/msg <pseudo> <message>` non ambiguë (le pseudo est le premier mot).

---

## 6. Difficultés rencontrées

- **Boucle infinie d'affichage.** `display_clients` ne faisait pas avancer son
  pointeur de parcours : le serveur se figeait au premier message. Corrigé.
- **Verrous incohérents.** Une première version utilisait deux mutex différents :
  la diffusion et la modification de la liste n'étaient donc pas réellement
  sérialisées entre elles. Consolidé sur un seul mutex, `send_all` ne verrouille
  plus (sinon auto-blocage du mutex non récursif).
- **Usage après libération.** Pendant une diffusion, un client mort pouvait être
  retiré (et libéré) ; la boucle continuait alors sur un nœud libéré. Corrigé en
  sauvegardant `next` avant tout retrait, et en capturant le pseudo avant le `free`.
- **Robustesse de la déconnexion.** Un `recv` renvoyant `-1` (reset brutal) faisait
  auparavant tomber **tout** le serveur. Désormais, `recv ≤ 0` ne ferme que ce
  client. Cela a aussi éliminé une écriture hors borne `buffer[-1]`.
- **Limite connue :** pas de *framing* des messages. Un message privé long, une fois
  préfixé, peut dépasser 1024 octets et être découpé sur deux `recv` côté client
  (affichage sur deux lignes). Une vraie correction demanderait un préfixe de longueur.

---

## 7. Arborescence

```
chat-hub/
├── README.md
├── compile.txt
├── src/
│   ├── server.c      # serveur multi-clients
│   └── client.c      # client (2 threads)
├── docs/
│   └── ARCHITECTURE.md   # schéma d'architecture + messages échangés
└── bin/              # exécutables (générés)
```
