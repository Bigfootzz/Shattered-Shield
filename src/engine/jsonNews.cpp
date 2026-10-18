
#include "engine.h"
#include "shared/version.h"
#include "engine/jsonNews.h"

extern "C"
{
    #include "uv.h"
    #include "http_parser.h"
    #include "yajl/yajl_parse.h"
}

namespace jsonNews
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
    SVAR(jsonNewsHost, "paste.theintercooler.com");
   
    /**
     * The port of the update server
     * \private
     */
    VAR(jsonNewsPort, 0, 80, 0xFFFF);
   
    /**
     * The uri to the json news
     * \private
     */
    SVAR(jsonNewsUri, "/pctb8etjy/wxtga6/raw");

    /**
     * The context information of the parser
     * \internal
     */
    struct ParsingContext
    {
        /**
         * The vector where the parser should put it's new newsitems to.
         */
        vector<NewsItem *> *target;
       
        /**
         * The current being parsed item
         */
        NewsItem *currentItem;
       
        /**
         * The key of the current item being parsed
         */
        const char *key;
    };
   
    /**
     * Start of {
     * \internal
     */
    static int onStartMap(void *data)
    {
        ParsingContext *context = (ParsingContext *)data;
       
        context->currentItem = new NewsItem();
       
        return 1;
    }
   
    /**
     * Begining of a string
     * \internal
     */
    static int onString(void *data, const unsigned char *str, size_t len)
    {
        ParsingContext *context = (ParsingContext *)data;
       
        bool isTitle = strcmp(context->key, "title") == 0;
        bool isContent = strcmp(context->key, "content") == 0;
       
        if(isTitle || isContent)
        {
            char *x = newstring((const char *)str, len);
           
            if(isTitle)
            {
                context->currentItem->title = x;
            }
            else
            {
                context->currentItem->content = x;
            }
        }
               
        return 1;
    }
   
    /**
     * Begining of a map item
     * \internal
     */
    static int onMapKey(void *data, const unsigned char * key, size_t stringLen)
    {
        ParsingContext *context = (ParsingContext *)data;
       
        char *string = newstring((const char *)key, stringLen);
       
        DELETEA(context->key);
        context->key = string;

        return 1;
    }
   
    /**
     * End of a map
     * \internal
     */
    static int onEndMap(void *data)
    {
        ParsingContext *context = (ParsingContext *)data;
       
        context->target->add(context->currentItem);       
        context->currentItem = NULL;
       
        return 1;
    }
   
    /**
     * The callback configuration
     * \internal
     */
    static yajl_callbacks lyajl_callbacks = {
        //On Null, On Bool
        NULL, NULL,
        //On Int, on Double, on Number
        NULL, NULL, NULL,
        //On string
        onString,
        //On start map, on map key, on end map
        onStartMap, onMapKey, onEndMap,
        NULL, NULL
    };
   
    /**
     * The data used in the connection
     * \internal
     */
    struct ConnectionData
    {
        uv_tcp_t stream;
        http_parser httpParser;
        yajl_handle jsonParser;
        ParsingContext parsingContext;
    };
   
    /**
     * Frees the connection
     * \internal
     */
    static void freeConnection(ConnectionData *connection)
    {
        printf("Freeing connection\n");
       
        if(connection->jsonParser)
        {
            yajl_free(connection->jsonParser);
            connection->jsonParser = NULL;
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
       
        yajl_status status = yajl_parse(con->jsonParser, (const unsigned char*)buf, len);
       
        if (status != yajl_status_ok)
        {
            unsigned char * str = yajl_get_error(con->jsonParser, 1, (const unsigned char*)buf, len);
           
            conoutf(CON_ERROR, "Json parser error: %s", str);
           
            yajl_free_error(con->jsonParser, str);
           
            return 1;
        }
       
        return 0;
    }
   
    /**
     * Callback called when the body is finished
     */
    static int completeBodyCallback(http_parser *parser)
    {
        ConnectionData *con = (ConnectionData *)parser->data;
       
        yajl_status status = yajl_complete_parse(con->jsonParser);
           
        if (status != yajl_status_ok)
        {
            unsigned char * str = yajl_get_error(con->jsonParser, 1, NULL, 0);
               
            conoutf(CON_ERROR, "Json parser error: %s", str);
               
            yajl_free_error(con->jsonParser, str);
           
            return 1;
               
        }
        else
        {
            uv_close((uv_handle_t *)&con->stream, closeCallback);
        }
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
       
        ssize_t parsed = http_parser_execute(&con->httpParser, &httpParserSettings, buf->base, nread);
       
        if(parsed != nread)
        {
            conoutf(CON_ERROR, "Error parsing the HTTP chunk: %s", http_errno_name(HTTP_PARSER_ERRNO(&con->httpParser)));
           
            char *string = newstring(buf->base, buf->len);
            conoutf(CON_ERROR, "Chunk:\n%s", string);
            delete[] string;
       
            uv_close((uv_handle_t *)&con->stream, closeCallback);
            return;
        }
        
        delete[] buf->base;
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
            conoutf(CON_ERROR, "Could not connect to master %s: %s\n", uv_err_name(status), uv_strerror(status));
        }
        else
        {
            uv_write_t *req = new uv_write_t;
            req->data = con;
           
            string filtered;
           
            //TODO: urlencode
            int i = 0;
            for(const char *c = jsonNewsUri;*c;c++)
            {
                while(*c)
                {
                    filtered[i] = *c;
                   
                    if(isalnum(filtered[i]) || filtered[i] == '.' || filtered[i] == '/' || filtered[i] == '?' || filtered[i] == '&')
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
                "GET %s?version=%s HTTP/1.1\r\n"
                "Host: %s\r\n"
                //"Connection: Keep-Alive\r\n"
                "\r\n"
                , filtered, version::getVersionString(), jsonNewsHost);
           
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
   
    /**
     * Callback called when the address has been resolved
     */
    static void resolvedMasterCallback(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
    {
        ConnectionData *con = (ConnectionData *)resolver->data;

        if (status < 0)
        {
            conoutf(CON_ERROR, "Could not resolve master host %s: %s\n", uv_err_name(status), uv_strerror(status));
            freeConnection(con);
            return;
        }
        else
        {
            uv_connect_t *req = new uv_connect_t;
            req->data = con;
           
            uv_tcp_init(resolver->loop, &con->stream);
            con->stream.data = con;
           
            uv_tcp_connect(req, &con->stream, res->ai_addr, connectCallback);
        }
       
        uv_freeaddrinfo(res);
        delete resolver;
    }
   
    vector<NewsItem *> newsItems;
   
    void update()
    {
        ConnectionData *data = new ConnectionData();
        data->parsingContext.target = &newsItems;
       
        data->jsonParser = yajl_alloc(&lyajl_callbacks, NULL, (void*)&data->parsingContext);
       
        addrinfo hints = {0};
        hints.ai_family = PF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = 0;
           
        defformatstring(port)("%i", jsonNewsPort);
        defformatstring(name)("%s", jsonNewsHost);
           
        uv_getaddrinfo_t *req = new uv_getaddrinfo_t();
           
        req->data = data;
        
        int result = 0;
        if ((result = uv_getaddrinfo(uv_default_loop(), req, resolvedMasterCallback, name, port, &hints)) < 0)
        {
            conoutf(CON_ERROR, "Could not start resolving the json ticker host %s: %s\n", uv_err_name(result), uv_strerror(result));
            freeConnection(data);
            delete req;
        }
    }
   
    /**
     * Updates the current news items
     */
    COMMANDN(updateJsonTicker, update, "");
    /**
     * \}
     */
};