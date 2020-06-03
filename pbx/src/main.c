#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "pbx.h"
#include "server.h"
#include "debug.h"

/* My own imports. */
#include "csapp.h"

static void terminate(int status);

/* SIGHUP handler for server. */
void sighup_server_handler(int sig)
{
    terminate(EXIT_SUCCESS);
}

/*
 * "PBX" telephone exchange simulation.
 *
 * Usage: pbx <port>
 */
int main(int argc, char* argv[]){
    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.

    char *port_num = NULL;
    /* First check if there are just 3 arguments (file name, -p, port #). */
    if (argc == 3)
    {
        /* Now check if first argument is -p flag. */
        if (strcmp(argv[1], "-p") == 0)
        {
            /* Now check if the port num is 1024 or greater (valid port num). If not, exit failure. */
            if (atoi(argv[2]) < 1024)
            {
                exit(EXIT_FAILURE);
            }
            else
            {
                /* Otherwise set port num as a string for future use. */
                port_num = argv[2];
            }
        }
        else
        {
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        exit(EXIT_FAILURE);
    }

    // Perform required initialization of the PBX module.
    debug("Initializing PBX...");
    pbx = pbx_init();

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function pbx_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.
    int listenfd;
    errno = 0;

    /* Now create, bind, and start listen for the server socket using open_listenfd. Check if return value is < 0. */
    if ((listenfd = open_listenfd(port_num)) < 0)
    {
        exit(EXIT_FAILURE);
    }

    /* Now install signal handler w/ sigaction b/c signal() doesn't work well with multithreading. */
    struct sigaction sighup_signal;
    sighup_signal.sa_handler = sighup_server_handler;
    sigaction(SIGHUP, &sighup_signal, NULL);

    int *connfdp;
    pthread_t thread_id;

    /* Now loop to accept client connections on the server socket, each thread running pbx_client_service(). */
    while (1)
    {
        /* malloc for one client. HAS TO BE FREED LATER ON IN EACH THREAD!! */
        connfdp = malloc(sizeof(int));

        if (connfdp == NULL)
        {
            exit(EXIT_FAILURE);
        }

        /* Now accept the client and create a new pthread for the client. */
        *connfdp = accept(listenfd, NULL, NULL);

        if (*connfdp < 0)
        {
            exit(EXIT_FAILURE);
        }

        pthread_create(&thread_id, NULL, pbx_client_service, connfdp);
    }

    fprintf(stderr, "You have to finish implementing main() "
	    "before the PBX server will function.\n");

    terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
void terminate(int status) {
    debug("Shutting down PBX...");
    pbx_shutdown(pbx);
    debug("PBX server terminating");
    exit(status);
}
