#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <sys/socket.h>

#include "pbx.h"
#include "server.h"
#include "debug.h"

/* Implementation of the pbx_client_service function which is the thread function that handles a client (TU). */
void *pbx_client_service(void *arg)
{
    int connfd = *((int *)arg);

    /* First FREE the storage that was malloced for the client file descriptor. */
    free(arg);

    /* Now detach the client thread so it can be implicitly reaped by the kernel. */
    pthread_detach(pthread_self());

    /* Now register the client file descriptor with the PBX module. */
    TU *client_TU;
    if ((client_TU = pbx_register(pbx, connfd)) == NULL)
    {
        exit(EXIT_FAILURE);
    }

    /* Get the TU's file descriptor to read input from the connection. Then open the file with given fd. */
    int TU_fd;
    if ((TU_fd = tu_fileno(client_TU)) < 0)
    {
        exit(EXIT_FAILURE);
    }

    FILE *fp;
    if ((fp = fdopen(TU_fd, "r")) == NULL)
    {
        exit(EXIT_FAILURE);
    }

    /* Now enter the service loop to parse the messages sent by the client and carry out the specified command.
    NOTE: The work done to carry out the command is done in the PBX MODULE! This includes responses back to the client
    so the server module SHOULDN'T be concerned w/ the function implementations. */
    while (1)
    {
        /* Read in client input w/ fgetc until reach EOL. */

        /* malloc for one char and realloc constantly for extra char. REMEMBER TO FREE! */
        char *client_msg = malloc(sizeof(char));

        /* If can't malloc for some reason, return NULL. */
        if (client_msg == NULL)
        {
            exit(EXIT_FAILURE);
        }

        int msg_size = 0;
        char *curr_msg_ptr = client_msg;
        int curr_char;

        while ((curr_char = fgetc(fp)) != '\r')
        {
            if (feof(fp))
            {
                free(client_msg);
                goto service_ended;
            }

            /* First, read the char in. Increment the msg size. Set the curr msg ptr to the new char. */
            msg_size++;
            *curr_msg_ptr = curr_char;

            /* Now, increment the curr msg ptr to move forward and realloc for 1 more char. */
            char *realloc_ptr = realloc((void *)client_msg, sizeof(char) * (msg_size + 1));

            /* If can't realloc for some reason, return NULL. */
            if (realloc_ptr == NULL)
            {
                exit(EXIT_FAILURE);
            }

            client_msg = realloc_ptr;
            curr_msg_ptr = client_msg + msg_size;
        }

        /* Read the next char \n to flush out the whole msg. */
        curr_char = fgetc(fp);

        /* After flushing \n AND reaching the \r, we end reading from the input and add a null terminator. */
        *curr_msg_ptr = '\0';

        /* NOTES FOR EACH TU FUNCTION in demo:
        1. pickup doesn't work if spaces after 'pickup'.
        2. hangup doesn't work if spaces after 'hangup'.
        3. dial ONLY works if at least 1 space after the 'dial' keyword. dial + 3 spaces + extension # WORKS.
        dial + extension # immediately afterwards doesn't work. Ex:dial   4 works but dial4 doesn't work.
        4. chat requires NO space afterwards for the message. if no msg after chat, chat will send empty msg.
        chat always sends a message. Therefore send string w/e it is after splitting it. */

        /* First check if msg is STRICTLY "pickup". If it is, call tu_pickup command. */
        if (strcmp(client_msg, tu_command_names[TU_PICKUP_CMD]) == 0)
        {
            int pickup_int;
            if ((pickup_int = tu_pickup(client_TU)) < 0)
            {
                /* If -1, then error occurred. exit failure! */
                free(client_msg);
                exit(EXIT_FAILURE);
            }
        }

        /* Now check if msg is STRICTLY "hangup". If it is, call tu_hangup command. */
        if (strcmp(client_msg, tu_command_names[TU_HANGUP_CMD]) == 0)
        {
            int hangup_int;
            if ((hangup_int = tu_hangup(client_TU)) < 0)
            {
                /* If -1, then error occurred. exit failure! */
                free(client_msg);
                exit(EXIT_FAILURE);
            }
        }

        /* Now check if msg is dial case. Remember this requires at least 1 space. So check strcmp first for "dial" then
        number. If not a number exit failure. */
        if (strncmp(client_msg, tu_command_names[TU_DIAL_CMD], strlen(tu_command_names[TU_DIAL_CMD])) == 0)
        {
            /* Now split by message by space to see if there is any space. Lets use the strtok_r function for reentrant. */
            char *second_half = client_msg;
            char *first_half = strtok_r(second_half, " ", &second_half);

            /* For cases where dial has no second half. Example command: "dial"
            FOR CASES WHERE DIAL # IS NOT A NUM DEALT W/ LATER (Ex: "dial q"). */

            /* After splitting, check if first half was dial. If it was not, go on to exit failure.
            Example command: "dial4" */
            if (strcmp(first_half, tu_command_names[TU_DIAL_CMD]) == 0)
            {
                /* Now check if the second half is an integer. */
                int second_half_int = atoi(second_half);

                /* Check if second half could be converted to an int. If it can't (= 0) exit failure
                (catches dial with bunch of spaces command: "dial    " */
                if (second_half_int > 0)
                {
                    /* If it is a valid #, proceed to call tu_dial command. */
                    int dial_int;
                    if ((dial_int = tu_dial(client_TU, second_half_int)) < 0)
                    {
                        /* If -1, then error occurred. exit failure! */
                        free(client_msg);
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }

        /* Lastly check if the msg is chat case. First check if "chat" is in the msg. If it is, then any valid chat is
        acceptable. chat does not require any spaces. */
        if (strncmp(client_msg, tu_command_names[TU_CHAT_CMD], strlen(tu_command_names[TU_CHAT_CMD])) == 0)
        {
            char *chat_msg = client_msg + strlen(tu_command_names[TU_CHAT_CMD]);

            /* Now cut off any excess space before the msg. any spaces afterwards is NOT cut off. For example,
            the message "     hey" -> "hey" BUT "    hey  there    " -> "hey  there    ". */
            while (*chat_msg == ' ')
            {
                chat_msg++;
            }

            /* Now send the chat message using the tu_chat command. */
            int chat_int;
            if ((chat_int = tu_chat(client_TU, chat_msg)) < 0)
            {
                free(client_msg);
                exit(EXIT_FAILURE);
            }
        }

        free(client_msg);
    }

    service_ended:
        fclose(fp);

        /* After service loop, unregister the client TU and close the connection! */
        int unregister_int;

        /* Check if unregistered successfully. */
        if((unregister_int = pbx_unregister(pbx, client_TU)) < 0)
        {
            exit(EXIT_FAILURE);
        }

        close(connfd);
        return NULL;
}