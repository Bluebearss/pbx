#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>
#include <semaphore.h>

#include "pbx.h"
#include "debug.h"
#include "csapp.h"

/* Each TU needs an extension number, which will be the same as its file descriptor.
Also, a TU needs to maintain its state name.
Also, a TU needs to maintain the extension number of the TU it is connecting with. */
struct tu {
    int extension_num;
    char *state_name;
    int connected_tu_extension_num;
    sem_t tu_mutex;
};

/* A PBX struct will contain a count of num of TU's registered.
It will also contain a list of all the TU's registered, WHERE THE INDEX REPRESENTS THE FD OF THE TU (MAPPING).
It also has a semaphore to perform asynchronous function while updating information. */
struct pbx {
    int TU_count;
    TU *client_TUs[PBX_MAX_EXTENSIONS + 4];
    sem_t mutex;
};

/* Makes a new PBX and initializes all its fields. */
PBX *pbx_init()
{
    /* Allocate memory for a PBX struct. WILL BE FREED IN PBX_SHUTDOWN()! */
    PBX *initial_pbx = malloc(sizeof(PBX));

    /* If can't malloc for initial PBX, exit. */
    if (initial_pbx == NULL)
    {
        exit(EXIT_FAILURE);
    }

    /* Initial count = 0 AND NULL out the TU arr for now. */
    initial_pbx -> TU_count = 0;

    for (int i = 0; i < PBX_MAX_EXTENSIONS + 4; i++)
    {
        initial_pbx -> client_TUs[i] = NULL;
    }

    /* Initialize semaphore w/ value 1. */
    sem_init(&(initial_pbx -> mutex), 0, 1);

    return initial_pbx;
}

/* Shut down PBX by freeing it from memory. */
void pbx_shutdown(PBX *pbx)
{
    /* First shutdown all TU extensions. Do so by unregistering it from pbx and shutting it down. */
    for (int i = 0; i < PBX_MAX_EXTENSIONS + 4; i++)
    {
        if ((pbx -> client_TUs[i]) != NULL)
        {
            /* Shutdown leads to pbx_unregister at server.c */
            // pbx_unregister(pbx, pbx -> client_TUs[i]);
            shutdown((pbx -> client_TUs[i]) -> extension_num, SHUT_RDWR);
        }
    }

    P(&(pbx -> mutex));
    /* Now wait for TU count to go down to 0. */
    while(pbx -> TU_count > 0)
    {
        ;
    }
    V(&(pbx -> mutex));

    /* After everything is shutdown, then free PBX. */
    free(pbx);
}

/* Registers a TU client to the PBX.
TU assigned an extension number and initialized to TU_ON_HOOK state.
Then the client is notified of the assigned extension number. */
TU *pbx_register(PBX *pbx, int fd)
{
    P(&(pbx -> mutex));

    /* Allocate memory for new TU. WILL BE FREED IN PBX_UNREGISTER! */
    TU *new_TU = malloc(sizeof(TU));

    /* If can't malloc for new TU OR max # of TU's for PBX then return NULL. */
    if (new_TU == NULL || (pbx -> TU_count) >= PBX_MAX_EXTENSIONS)
    {
        return NULL;
    }

    /* Assign extension number (same as fd) and state name to TU_ON_HOOK state. connected_tu state now -1 for now. */
    /* Initialize TU semaphore w/ value 1. */
    new_TU -> extension_num = fd;
    new_TU -> state_name = tu_state_names[TU_ON_HOOK];
    new_TU -> connected_tu_extension_num = -1;
    sem_init(&(new_TU -> tu_mutex), 0, 1);

    /* Now set new TU in PBX WHERE THE INDEX IS THE FD/EXTENSION # OF THE TU (MAPPING) and increment TU count. */
    pbx -> client_TUs[new_TU -> extension_num] = new_TU;
    pbx -> TU_count++;

    /* Now print message! */
    dprintf(fd, "%s %d\n", new_TU -> state_name, new_TU -> extension_num);

    V(&(pbx -> mutex));

    return new_TU;
}

/* Unregisters a TU client from the PBX.
Do the reverse of registering and REMEMBER TO FREE the TU AND CHANGE STATE OF OTHER TU! */
int pbx_unregister(PBX *pbx, TU *tu)
{
    /* If invalid TU, then return -1. */
    if (tu == NULL)
    {
        return -1;
    }

    P(&(pbx -> mutex));
    P(&(tu -> tu_mutex));

    /* Before freeing the TU, change state of other TU. */
    TU *peer_TU = pbx -> client_TUs[tu -> connected_tu_extension_num];

    /* Only change and print new state if peer TU is not NULL. */
    if (peer_TU != NULL)
    {
        P(&(peer_TU -> tu_mutex));

        /* If peer TU was in RINGING state, it was the called TU. Go to ON HOOK state. */
        if (strcmp(peer_TU -> state_name, tu_state_names[TU_RINGING]) == 0)
        {
            peer_TU -> state_name = tu_state_names[TU_ON_HOOK];
            dprintf(peer_TU -> extension_num, "%s %d\n", peer_TU -> state_name, peer_TU -> extension_num);
        }

        /* If peer TU was in RING BACK state, it was the calling TU. Go to DIAL TONE state. */
        /* If peer TU was in CONNECTED state, there was a peer connection. Set peer to DIAL TONE state. */
        if (strcmp(peer_TU -> state_name, tu_state_names[TU_RING_BACK]) == 0 ||
            strcmp(peer_TU -> state_name, tu_state_names[TU_CONNECTED]) == 0)
        {
            peer_TU -> state_name = tu_state_names[TU_DIAL_TONE];
            dprintf(peer_TU -> extension_num, "%s\n", peer_TU -> state_name);
        }

        V(&(peer_TU -> tu_mutex));
    }

    free(tu);

    /* Now set the TU at its index/fd/extension # to NULL. After, decrement the count. */
    pbx -> client_TUs[tu -> extension_num] = NULL;
    pbx -> TU_count--;

    V(&(tu -> tu_mutex));
    V(&(pbx -> mutex));
    return 0;
}

/* Gets the TU's fd which is the same as the extension # which is the same as the index @ the TU array. */
int tu_fileno(TU *tu)
{
    /* If invalid tu, return -1. */
    if (tu == NULL)
    {
        return -1;
    }

    P(&(tu -> tu_mutex));

    int tu_fd = tu -> extension_num;
    V(&(tu -> tu_mutex));

    return tu_fd;
}

/* Gets the TU's extension # which is the same as the TU's fd which is the same as the index @ the TU array. */
int tu_extension(TU *tu)
{
    /* If invalid tu, return -1. */
    if (tu == NULL)
    {
        return -1;
    }

    P(&(tu -> tu_mutex));

    int tu_extension_num = tu -> extension_num;
    V(&(tu -> tu_mutex));

    return tu_extension_num;
}

/* client picks up the TU, leaving it OFF THE HOOK. Situations:
If TU_ON_HOOK state -> TU_DIAL_TONE state
If TU_RINGING state -> TU_CONNECTED state (The Calling TU will go to the TU_CONNECTED state as well).
If in any other state -> SAME STATE.
Then, w/e state it was in, print message of new or same state. If new state of calling TU is TU_CONNECTED,
calling TU notified of its new state as well. */
int tu_pickup(TU *tu)
{
    /* If tu was NULL. */
    if (tu == NULL)
    {
        return -1;
    }

    P(&(tu -> tu_mutex));

    /* Check if on TU_ON_HOOK state. Change to TU_DIAL_TONE state. */
    if (strcmp(tu -> state_name, tu_state_names[TU_ON_HOOK]) == 0)
    {
        tu -> state_name = tu_state_names[TU_DIAL_TONE];
        dprintf(tu -> extension_num, "%s\n", tu -> state_name);
    }
    else if (strcmp(tu -> state_name, tu_state_names[TU_RINGING]) == 0)
    {
        tu -> state_name = tu_state_names[TU_CONNECTED];
        /* Now print message that you are connected to the CALLING TU! NOT URSELF! */
        int calling_TU_extension_num = tu -> connected_tu_extension_num;
        dprintf(tu -> extension_num, "%s %d\n", tu -> state_name, calling_TU_extension_num);

        V(&(tu -> tu_mutex));

        /* Now grab other TU from global PBX variable. REMEMBER TO MUTEX FOR EXCLUSIVE ACCESS! */
        P(&(pbx -> mutex));

        TU *calling_TU = pbx -> client_TUs[calling_TU_extension_num];

        /* If calling TU is NULL. */
        if (calling_TU == NULL)
        {
            return -1;
        }

        /* Mutex the Calling TU now. */
        P(&(calling_TU -> tu_mutex));

        /* If calling TU is in TU_RING_BACK state, set to TU_CONNECTED state and print CONNECTED message. */
        if (strcmp(calling_TU -> state_name, tu_state_names[TU_RING_BACK]) == 0)
        {
            calling_TU -> state_name = tu_state_names[TU_CONNECTED];

            /* Now print message that you are connected to the called TU! NOT URSELF! */
            dprintf(calling_TU -> extension_num, "%s %d\n", calling_TU -> state_name,
                calling_TU -> connected_tu_extension_num);
        }

        V(&(calling_TU -> tu_mutex));
        V(&(pbx -> mutex));

        return 0;
    }
    else if (strcmp(tu -> state_name, tu_state_names[TU_CONNECTED]) == 0)
    {
        /* If in TU_CONNECTED, print connected_tu extension # as well. */
        dprintf(tu -> extension_num, "%s %d\n", tu -> state_name, tu -> connected_tu_extension_num);
    }
    else
    {
        /* Any other state, print message of same state. */
        dprintf(tu -> extension_num, "%s\n", tu -> state_name);
    }

    V(&(tu -> tu_mutex));

    return 0;
}

/* Replaces the handset on the switchhook. Situations:
If TU_CONNECTED state -> TU_ON_HOOK state. THE PEER TU WHO DIDNT HANGUP GOES TO TU_DIAL_TONE STATE!!
If TU_RING_BACK state -> TU_ON_HOOK state. THE CALLING TU ON TU_RINGING state GOES TO TU_ON_HOOK STATE!!
If TU_RINGING state -> TU_ON_HOOK state. THE CALLING TU ON TU_RING_BACK state GOES TO TU_DIAL_TONE STATE!!
If TU_DIAL_TONE, TU_BUSY_SIGNAL, or TU_ERROR state -> TU_ON_HOOK state
Any other state goes to SAME state.
Then, w/e state it was in, print message of new or same state.
If prev state of TU was TU_CONNECTED, TU_RING_BACK, or TU_RINGING then PEER TU ALSO GETS A MESSAGE PRINTED OF NEW STATE! */
int tu_hangup(TU *tu)
{
    /* If tu was NULL, return -1. */
    if (tu == NULL)
    {
        return -1;
    }

    P(&(tu -> tu_mutex));

    /* If TU in connected state, go to on hook state and make peer TU go to dial tone state! Print message too. */
    if (strcmp(tu -> state_name, tu_state_names[TU_CONNECTED]) == 0)
    {
        tu -> state_name = tu_state_names[TU_ON_HOOK];
        dprintf(tu -> extension_num, "%s %d\n", tu -> state_name, tu -> extension_num);

        int peer_TU_extension_num = tu -> connected_tu_extension_num;

        V(&(tu -> tu_mutex));

        /* Now make other TU transition to dial tone state. */
        P(&(pbx -> mutex));

        TU *peer_TU = pbx -> client_TUs[peer_TU_extension_num];

        /* If peer TU is NULL, return -1. */
        if (peer_TU == NULL)
        {
            return -1;
        }

        /* Mutex the peer TU now. */
        P(&(peer_TU -> tu_mutex));

        /* peer TU set to TU_DIAL_TONE state and print DIAL TONE message. */
        peer_TU -> state_name = tu_state_names[TU_DIAL_TONE];

        /* Now print message that you are dial tone state. */
        dprintf(peer_TU -> extension_num, "%s\n", peer_TU -> state_name);

        V(&(peer_TU -> tu_mutex));
        V(&(pbx -> mutex));

        return 0;
    }
    else if (strcmp(tu -> state_name, tu_state_names[TU_RING_BACK]) == 0)
    {
        /* If TU in ring back state, go to on hook state and make peer TU whose on ringing state go to on hook state!
        Print message too. */
        tu -> state_name = tu_state_names[TU_ON_HOOK];
        dprintf(tu -> extension_num, "%s %d\n", tu -> state_name, tu -> extension_num);

        int peer_TU_extension_num = tu -> connected_tu_extension_num;

        V(&(tu -> tu_mutex));

        /* Now make peer TU transition to on hook state. */
        P(&(pbx -> mutex));

        TU *peer_TU = pbx -> client_TUs[peer_TU_extension_num];

        /* If peer TU is NULL, return -1. */
        if (peer_TU == NULL)
        {
            return -1;
        }

        /* Mutex the peer TU now. */
        P(&(peer_TU -> tu_mutex));

        /* If peer TU is in TU_RINGING state, set to TU_ON_HOOK state and print TU_ON_HOOK message. */
        if (strcmp(peer_TU -> state_name, tu_state_names[TU_RINGING]) == 0)
        {
            /* peer TU set to TU_ON_HOOK state and print TU_ON_HOOK message. */
            peer_TU -> state_name = tu_state_names[TU_ON_HOOK];

            /* Now print message that you are TU_ON_HOOK state. */
            dprintf(peer_TU -> extension_num, "%s %d\n", peer_TU -> state_name, peer_TU -> extension_num);
        }

        V(&(peer_TU -> tu_mutex));
        V(&(pbx -> mutex));

        return 0;
    }
    else if (strcmp(tu -> state_name, tu_state_names[TU_RINGING]) == 0)
    {
        /* If TU in ringing state, go to on hook state and make peer TU whose on ring back state go to dial tone state!
        Print message too. */
        tu -> state_name = tu_state_names[TU_ON_HOOK];
        dprintf(tu -> extension_num, "%s %d\n", tu -> state_name, tu -> extension_num);

        int peer_TU_extension_num = tu -> connected_tu_extension_num;

        V(&(tu -> tu_mutex));

        /* Now make peer TU transition to dial tone state. */
        P(&(pbx -> mutex));

        TU *peer_TU = pbx -> client_TUs[peer_TU_extension_num];

        /* If peer TU is NULL, return -1. */
        if (peer_TU == NULL)
        {
            return -1;
        }

        /* Mutex the peer TU now. */
        P(&(peer_TU -> tu_mutex));

        /* If peer TU is in TU_RING_BACK state, set to TU_DIAL_TONE state and print TU_DIAL_TONE message. */
        if (strcmp(peer_TU -> state_name, tu_state_names[TU_RING_BACK]) == 0)
        {
            /* peer TU set to TU_DIAL_TONE state and print TU_DIAL_TONE message. */
            peer_TU -> state_name = tu_state_names[TU_DIAL_TONE];

            /* Now print message that you are TU_DIAL_TONE state. */
            dprintf(peer_TU -> extension_num, "%s\n", peer_TU -> state_name);
        }

        V(&(peer_TU -> tu_mutex));
        V(&(pbx -> mutex));

        return 0;
    }
    else
    {
        /* Any other state (TU_DIAL_TONE, TU_BUSY_SIGNAL, TU_ERROR, or TU_ON_HOOK) goes to TU_ON_HOOK state.
        Then, print the message of the on hook state. */
        tu -> state_name = tu_state_names[TU_ON_HOOK];
        dprintf(tu -> extension_num, "%s %d\n", tu -> state_name, tu -> extension_num);
    }

    V(&(tu -> tu_mutex));

    return 0;
}

/* dials TU whose extension number is given ext. */
int tu_dial(TU *tu, int ext)
{
    /* If tu was NULL, return -1. */
    if (tu == NULL)
    {
        return -1;
    }

    P(&(tu -> tu_mutex));

    /* First check if TU in dial tone state. If not, simply reprint the same state. */
    if (strcmp(tu -> state_name, tu_state_names[TU_DIAL_TONE]) == 0)
    {
        P(&(pbx -> mutex));

        /* Check if any TU has given ext #. */
        TU *peer_TU;

        /* Check if within array bounds. If not, go to error state and print error state. */
        if (ext >= 0 && ext < PBX_MAX_EXTENSIONS + 4)
        {
            peer_TU = pbx -> client_TUs[ext];

            /* If NULL TU dialing to, go to error state and print error state. */
            if (peer_TU == NULL)
            {
                tu -> state_name = tu_state_names[TU_ERROR];
                dprintf(tu -> extension_num, "%s\n", tu -> state_name);
            }
            else
            {
                /* Otherwise proceed to dial the other TU. */
                P(&(peer_TU -> tu_mutex));

                /* First set each other's connection TU extension #'s. */
                tu -> connected_tu_extension_num = ext;
                peer_TU -> connected_tu_extension_num = tu -> extension_num;

                /* Check if the peer TU was in TU_ON_HOOK state. If so,
                calling TU goes from TU_DIAL_TONE state -> TU_RING_BACK state AND
                peer TU goes from TU_ON_HOOK state -> TU_RINGING state. */
                if (strcmp(peer_TU -> state_name, tu_state_names[TU_ON_HOOK]) == 0)
                {
                    tu -> state_name = tu_state_names[TU_RING_BACK];
                    dprintf(tu -> extension_num, "%s\n", tu -> state_name);

                    peer_TU -> state_name = tu_state_names[TU_RINGING];
                    dprintf(peer_TU -> extension_num, "%s\n", peer_TU -> state_name);
                }
                else
                {
                    /* Otherwise, calling TU goes to TU_BUSY_SIGNAL state and peer TU same state. */
                    tu -> state_name = tu_state_names[TU_BUSY_SIGNAL];
                    dprintf(tu -> extension_num, "%s\n", tu -> state_name);
                }

                V(&(peer_TU -> tu_mutex));
            }
        }
        else
        {
            tu -> state_name = tu_state_names[TU_ERROR];
            dprintf(tu -> extension_num, "%s\n", tu -> state_name);
        }

        V(&(tu -> tu_mutex));
        V(&(pbx -> mutex));

        return 0;
    }
    else if (strcmp(tu -> state_name, tu_state_names[TU_ON_HOOK]) == 0)
    {
        /* ON HOOK state. */
        dprintf(tu -> extension_num, "%s %d\n", tu -> state_name, tu -> extension_num);
    }
    else if (strcmp(tu -> state_name, tu_state_names[TU_CONNECTED]) == 0)
    {
        /* CONNECTED state. */
        dprintf(tu -> extension_num, "%s %d\n", tu -> state_name, tu -> connected_tu_extension_num);
    }
    else
    {
        /* Any other state. */
        dprintf(tu -> extension_num, "%s\n", tu -> state_name);
    }

    V(&(tu -> tu_mutex));

    return 0;
}

/* TU's can chat over a peer connection. If not TU_CONNECTED state, return -1.
Else, send message to peer TU thru the network connection. States unchanged and TU sending chat prints curr state. */
int tu_chat(TU *tu, char *msg)
{
    /* If tu was NULL, return -1. */
    if (tu == NULL)
    {
        return -1;
    }

    P(&(tu -> tu_mutex));

    /* Check if TU_CONNECTED state. If not, return -1. */
    if (strcmp(tu -> state_name, tu_state_names[TU_CONNECTED]) == 0)
    {
        /* Now print message that you are connected to the CALLING TU! NOT URSELF! */
        int peer_TU_extension_num = tu -> connected_tu_extension_num;
        dprintf(tu -> extension_num, "%s %d\n", tu -> state_name, peer_TU_extension_num);

        V(&(tu -> tu_mutex));

        /* Now grab peer TU from global PBX variable. REMEMBER TO MUTEX FOR EXCLUSIVE ACCESS! */
        P(&(pbx -> mutex));

        TU *peer_TU = pbx -> client_TUs[peer_TU_extension_num];

        /* If peer TU is NULL. */
        if (peer_TU == NULL)
        {
            return -1;
        }

        /* Mutex the peer TU now. */
        P(&(peer_TU -> tu_mutex));

        /* If peer TU is in TU_CONNECTED state, print chat message. */
        if (strcmp(peer_TU -> state_name, tu_state_names[TU_CONNECTED]) == 0)
        {
            dprintf(peer_TU -> extension_num, "CHAT %s\n", msg);
        }

        V(&(peer_TU -> tu_mutex));
        V(&(pbx -> mutex));

        return 0;
    }
    else if (strcmp(tu -> state_name, tu_state_names[TU_ON_HOOK]) == 0)
    {
        dprintf(tu -> extension_num, "%s %d\n", tu -> state_name, tu -> extension_num);
    }
    else
    {
        dprintf(tu -> extension_num, "%s\n", tu -> state_name);
    }

    V(&(tu -> tu_mutex));
    return -1;
}