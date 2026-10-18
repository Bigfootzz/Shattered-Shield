
#include "engine.h"

#include "SDL_mixer.h"
#define MAXVOL MIX_MAX_VOLUME
extern float musicvol;

extern "C"
{
    #include "uv.h"
    #include "http_parser.h"
    #include "yajl/yajl_parse.h"
}

namespace webRadio
{
    /**
     * A json news ticker
     * \defgroup json-news Json news ticker
     * \{
     */
   
    /**
     * The host to the update server
     * \private
     */
    SVARP(webRadioHost, "stream-tx4.radioparadise.com");
   
    /**
     * The port of the update server
     * \private
     */
    VARP(webRadioPort, 0, 80, 0xFFFF);
   
    /**
     * The uri to the json news
     * \private
     */
    SVARP(webRadioUri, "/rp_32.ogg");

    /**
     * The data used in the connection
     * \internal
     */
    struct ConnectionData
    {
        uv_tcp_t stream;
        http_parser httpParser;
        SDL_RWops *rw;
        ::stream *buffer;
    };

    /**
     * Frees the connection
     * \internal
     */
    static void freeConnection(ConnectionData *connection)
    {
        printf("Freeing connection\n");

        if(connection->rw)
        {
            SDL_FreeRW(connection->rw);
        }
        delete connection;
    }

    /**
     * Callback called when the tcp connection is closed
     */
    static void closeCallback(uv_handle_t *handle)
    {
        ConnectionData *con = (ConnectionData *)handle->data;
        freeConnection(con);
    }

    /**
     * Callback called on a body fragment
     */
    static int readBodyCallback(http_parser *parser, const char *buf, size_t len)
    {
        ConnectionData *con = (ConnectionData *)parser->data;

        if(!con->buffer)
        {
            con->buffer = openfile("soundstream", "wb");
            if(!con->buffer)
            {
                printf("Could not open sound stream\n");
                return -1;
            }
        }

        long pos = con->buffer->tell();
        //printf("Readin %li \n", pos);
        con->buffer->seek(0, SEEK_END);
        con->buffer->write(buf, len);
        con->buffer->seek(pos, SEEK_SET);

//         if(con->rw == NULL && con->buffer->size() > 10000)
//         {
//             con->rw = con->buffer->rwops();
//
//             Mix_Music *music = Mix_LoadMUS_RW(con->rw);
//             if(!music)
//             {
//                 printf("Unable to play music: %s\n", SDL_GetError());
//                 return -1;
//             }
//             Mix_PlayMusic(music, -1);
//             Mix_VolumeMusic((musicvol*MAXVOL)/255);
//         }

//         char *string = newstring(buf, len);
//         conoutf(CON_ERROR, "Chunk:\n%s", string);
//         delete[] string;

        return 0;
    }
   
    /**
     * Callback called when the body is finished
     */
    static int completeBodyCallback(http_parser *parser)
    {
        ConnectionData *con = (ConnectionData *)parser->data;
        uv_close((uv_handle_t *)&con->stream, closeCallback);
        return 0;
    }

    /**
     * Callback called on receiving a tcp chunk.
     */
    static void readCallback(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf)
    {
        static http_parser_settings httpParserSettings = {0};
        httpParserSettings.on_body = readBodyCallback;
        httpParserSettings.on_message_complete = completeBodyCallback;

        ConnectionData *con = (ConnectionData *)stream->data;

        if( nread < 0 )
        {
            uv_close((uv_handle_t *)&con->stream, closeCallback);
            return;
        }

        char *bufferString = buf->base;
        size_t bufferLen = buf->len;

        int skip = 0;
        if(con->httpParser.nread == 0 && bufferString[0] == 'I' && bufferString[1] == 'C' && bufferString[2] == 'Y' && bufferString[3] == ' ' && bufferString[4] == '2' && bufferString[5] == '0' && bufferString[6] == '0' && bufferString[7] == ' ' && bufferString[8] == 'O' && bufferString[9] == 'K' && bufferString[10] == '\r' && bufferString[11] == '\n')
        {
            skip = 12;
            bufferString += skip;
            bufferLen -= skip;
            const char *msg = "HTTP/1.1 200 OK";
            ssize_t len = strlen(msg);
            ssize_t parsed = http_parser_execute(&con->httpParser, &httpParserSettings, msg, len);

            if(parsed != len)
            {
                conoutf(CON_ERROR, "Error parsing the HTTP chunk: %s", http_errno_name(HTTP_PARSER_ERRNO(&con->httpParser)));

                char *string = newstring(bufferString, bufferLen);
                conoutf(CON_ERROR, "Chunk:\n%s", string);
                delete[] string;

                uv_close((uv_handle_t *)&con->stream, closeCallback);
                return;
            }
        }

        ssize_t parsed = http_parser_execute(&con->httpParser, &httpParserSettings, bufferString, nread);

        if(parsed != nread)
        {
            conoutf(CON_ERROR, "Error parsing the HTTP chunk: %s", http_errno_name(HTTP_PARSER_ERRNO(&con->httpParser)));

            char *string = newstring(bufferString, bufferLen);
            conoutf(CON_ERROR, "Chunk:\n%s", string);
            delete[] string;

            uv_close((uv_handle_t *)&con->stream, closeCallback);
            return;
        }

        char *p = bufferString - skip;
        DELETEP(p);
    }
   
    /**
     * Callback called when uv wants another chunk
     */
    static void allocCallback(uv_handle_t* handle, size_t suggested_size, uv_buf_t *buf)
    {
        buf->base = new char [suggested_size];
        buf->len = suggested_size;
    }
   
    /**
     * Callback called on write
     */
    static void writeCallback(uv_write_t *req, int status)
    {
        if(status < 0)
        {
            ConnectionData *con = (ConnectionData *)req->data;
            conoutf(CON_ERROR, "Could not send GET %s: %s\n", uv_err_name(status), uv_strerror(status));
            uv_close((uv_handle_t *)&con->stream, closeCallback);
        }
        //delete[] req->buf.base;
        //delete req->buf;
        delete req;
    }
   
    /**
     * Callback called when connected to the tcp server
     */
    static void connectCallback(uv_connect_t* req, int status)
    {
        ConnectionData *con = (ConnectionData *)req->data;
       
        if(status < 0)
        {
            conoutf(CON_ERROR, "Could not connect to web radio %s: %s\n", uv_err_name(status), uv_strerror(status));
        }
        else
        {
            uv_write_t *req = new uv_write_t;
            req->data = con;
           
            string filtered;
           
            //TODO: urlencode
            int i = 0;
            for(const char *c = webRadioUri;*c;c++)
            {
                while(*c)
                {
                    filtered[i] = *c;
                   
                    if(isalnum(filtered[i]) || filtered[i] == '.' || filtered[i] == '/' || filtered[i] == '?' || filtered[i] == '&' || filtered[i] == '_')
                    {
                        break;
                    }
                   
                    c++;
                }
               
                if(*c)
                {
                    i++;
                }
            }
            filtered[i] = '\0';
           
            defformatstring(cmd)(
                "GET %s HTTP/1.1\r\n"
                "Host: %s\r\n"
                //"Connection: Keep-Alive\r\n"
                "\r\n"
                , filtered, webRadioHost);
           
            printf("Writing: %s", cmd);
            uv_buf_t buf;
            buf.len = strlen(cmd);
            buf.base = new char [buf.len];
            strncpy(buf.base, cmd, buf.len);

            int result = 0;
            if((result = uv_write(req, (uv_stream_t *)&con->stream, &buf, 1, writeCallback)) < 0)
            {
                conoutf(CON_ERROR, "Could not send master request %s: %s\n", uv_err_name(result), uv_strerror(result));
                uv_close((uv_handle_t *)&con->stream, closeCallback);
                return;
            }

            http_parser_init(&con->httpParser, HTTP_RESPONSE);
            con->httpParser.data = con;

            uv_read_start((uv_stream_t *)&con->stream, allocCallback, readCallback);
        }

        delete req;
    }
   
    static void resolvedMasterCallback(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
    {
        ConnectionData *con = (ConnectionData *)resolver->data;
        if (status < 0)
        {
            conoutf(CON_ERROR, "Could not resolve radio host %s: %s\n", uv_err_name(status), uv_strerror(status));
            freeConnection(con);
            return;
        }
        else
        {
            uv_connect_t *req = new uv_connect_t;
            req->data = con;

            char addr[17] = {'\0'};
            uv_ip4_name((struct sockaddr_in*) res->ai_addr, addr, 16);
            printf("Connecting to: %s\n", addr);

            uv_tcp_init(resolver->loop, &con->stream);
            con->stream.data = con;
           
            uv_tcp_connect(req, &con->stream, res->ai_addr, connectCallback);
        }
       
        uv_freeaddrinfo(res);
        delete resolver;
    }
   
    void update()
    {
        ConnectionData *data = new ConnectionData();

        addrinfo hints = {0};
        hints.ai_family = PF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = 0;
       
        defformatstring(port)("%i", webRadioPort);
        defformatstring(name)("%s", webRadioHost);
       
        uv_getaddrinfo_t *req = new uv_getaddrinfo_t();
       
        req->data = data;

        int status = 0;
        if ((status = uv_getaddrinfo(uv_default_loop(), req, resolvedMasterCallback, name, port, &hints)) < 0)
        {
            conoutf(CON_ERROR, "Could not start resolving the web radio host: %s: %s\n", uv_err_name(status), uv_strerror(status));
            freeConnection(data);
            delete req;
        }
    }
   
    /**
     * Updates the current news items
     */
    COMMANDN(updateWebRadio, update, "");
};