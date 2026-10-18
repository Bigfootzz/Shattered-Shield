#include "engine.h"
#include "shared/version.h"

extern "C"
{
    #include "uv.h"
    #include "http_parser.h"
    #include "yajl/yajl_parse.h"
}

void addserver( const char *name, int port, const char *password, bool keep)
{
    
}

void writeservercfg()
{
}

namespace serverbrowser
{
    struct MasterServerConnection;
    
    static MasterServerConnection *connection = NULL;
    
    SVARP(mastername, server::defaultmaster());
    VARP(masterport, 0, server::masterport(), 65535);
    SVAR(masterpath, "/");
    string port;
    string name;
    
    struct MasterServerConnection
    {
        enum Status
        {
            Idle,
            Resolving,
            Connecting,
            Connected,
            Reading,
            Failed,
        };
        
        Status status;
        uv_tcp_t *handle;
        uv_stream_t *stream;
        uv_loop_t *loop;
        http_parser *parser;
        yajl_handle jsonParser;
        
        MasterServerConnection() : status(MasterServerConnection::Idle), handle(NULL), stream(NULL), parser(NULL), jsonParser(NULL)
        {
        }
        
        ~MasterServerConnection()
        {
            if(stream)
            {
                uv_read_stop(stream);
            }
            
            if(parser)
            {
                delete parser;
            }
        }
    };
    
    static void writeCallback(uv_write_t* req, int status)
    {
        if(status < 0)
        {
            //MasterServerConnection *con = (MasterServerConnection *)req->data;
            conoutf(CON_ERROR, "Could not send GET %s: %s\n", uv_err_name(status), uv_strerror(status));
        }
        //delete[] req->buf.base;
        //delete req->buf;
        delete req;
    }

    struct ServerField
    {
        string key;
        string value;
    };
    
    struct ServerInfo
    {
        const char *uuid;
        bool wasUpdated;
        vector<ServerField> info;

        ~ServerInfo()
        {
            DELETEA(uuid);
        }
    };
    
    hashtable<const char *, ServerInfo *> servers;
    ServerInfo *currentServer = NULL;
    
    static void writeGet(MasterServerConnection *con)
    {
        con->status = MasterServerConnection::Reading;
        
        if(!con->parser)
        {
            con->parser = new http_parser;
            con->parser->data = con;
        }
        
        uv_write_t *req = new uv_write_t;
        req->data = con;
        
        string filtered;
        
        //TODO: urlencode
        int i = 0;
        for(const char *c = masterpath;*c;c++)
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
            "GET %s?version=%s&protocol=%i HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: ReveladeRevolution-client/%s (protocol-%i)\r\n"
            //"Connection: Keep-Alive\r\n"
            "\r\n"
            , filtered, version::getVersionString(), server::protocolversion(), mastername
            , version::getVersionString(), server::protocolversion());
        
        uv_buf_t buf;
        buf.len = strlen(cmd);
        buf.base = new char [buf.len];
        strncpy(buf.base, cmd, buf.len);
        
        //Reset
        http_parser_init(con->parser, HTTP_RESPONSE);
        
        enumerate(servers, ServerInfo *, server,
        {
            server->wasUpdated = false;
        });
        
        int err = 0;
        if((err = uv_write(req, (uv_stream_t *)con->handle, &buf, 1, writeCallback)) < 0)
        {
            conoutf(CON_ERROR, "Could not send master request %s: %s\n", uv_err_name(err), uv_strerror(err));
        }
    }
    
    static void allocCallback(uv_handle_t* handle, size_t suggested_size, uv_buf_t *buf)
    {
        buf->base = new char [suggested_size];
        buf->len = suggested_size;
    }
    struct ParsingContext
    {
        const char *key;
    };
    
    ParsingContext context = { 0 };
    
    static int ignore(void *data)
    {
        return 1;
    }
    
    static int onNull(void *data)
    {
        ASSERT(currentServer);
        copystring(currentServer->info.last().value, "0");
        
        return 1;
    }
    
    static int onBool(void *data, int val)
    {
        ASSERT(currentServer);
        copystring(currentServer->info.last().value, val ? "1" : "0");
        
        return 1;

    }

    
    static int onNumber(void *data, const char *str, size_t len)
    {
        ASSERT(currentServer);
        
        char * x = newstring(str, len);
        copystring(currentServer->info.last().value, x);
        delete[] x;
        
        return 1;
    }
    
    static int onString(void *data, const unsigned char *str, size_t len)
    {
        ASSERT(currentServer);
        
        char * x = newstring((const char *)str, len);
        copystring(currentServer->info.last().value, x);
        
        if(context.key && 0 == strcmp(context.key, "uuid"))
        {
            currentServer->uuid = x;
        }
        else
        {
            delete[] x;
        }
        
        return 1;
    }
    
    static int onStartMap(void *data)
    {
        currentServer = new ServerInfo;
        
        return 1;
    }
    
    static int onMapKey(void *data, const unsigned char * key, size_t stringLen)
    {
        currentServer->info.add();
        
        char *string = newstring((const char *)key, stringLen);
        
        DELETEA(context.key);
        context.key = string;
        
        copystring(currentServer->info.last().key, string);
        
        return 1;
    }
    
    static int onEndMap(void *data)
    {
        ASSERT(currentServer);
        
        ServerInfo **server = servers.access(currentServer->uuid);
        
        if(server != NULL)
        {
            servers.remove(currentServer->uuid);
            //delete server; // TODO do we need this?
        }
        
        servers[currentServer->uuid] = currentServer;
        
        currentServer->wasUpdated = true;
        
        return 1;
    }
    
    static yajl_callbacks lyajl_callbacks = {
        onNull, onBool,
        NULL, NULL, onNumber,
        onString,
        onStartMap, onMapKey, onEndMap,
        ignore, ignore
    };
    
    static int readBodyCallback(http_parser *parser, const char *buf, size_t len)
    {
        MasterServerConnection *con = (MasterServerConnection *)parser->data;
        
        if(!con->jsonParser)
        {
            con->jsonParser = yajl_alloc(&lyajl_callbacks, NULL, (void*)con);
        }
        
        yajl_status status = yajl_parse(con->jsonParser, (const unsigned char*)buf, len);
        
        if (status != yajl_status_ok)
        {
            con->status = MasterServerConnection::Failed;
            unsigned char * str = yajl_get_error(con->jsonParser, 1, (const unsigned char*)buf, len);
            
            conoutf(CON_ERROR, "Json parser error: %s", str);
            
            char *string = newstring(len);
            strncpy(string, buf, len);
            string[len+1] = '\0';
            
            yajl_free_error(con->jsonParser, str);
        }
        

        
        return 0;
    }
    
    static int completeBodyCallback(http_parser *parser)
    {
        MasterServerConnection *con = (MasterServerConnection *)parser->data;
        
        if(con->status == MasterServerConnection::Reading)
        {
            yajl_status status = yajl_complete_parse(con->jsonParser);
            
            if (status != yajl_status_ok)
            {
                con->status = MasterServerConnection::Failed;
                unsigned char * str = yajl_get_error(con->jsonParser, 1, NULL, 0);
                
                conoutf(CON_ERROR, "Json parser error: %s", str);
                
                yajl_free_error(con->jsonParser, str);
                
            }
            else
            {
                connection->status = MasterServerConnection::Connected;
            
                //Time out servers
                enumerate(servers, ServerInfo *, server,
                {
                    if (!server->wasUpdated)
                    {
                        servers.remove(server->uuid);
                        delete server;
                    }
                });
            }
                
            if(con->jsonParser)
            {
                yajl_free(con->jsonParser);
                con->jsonParser = NULL;
            }
        }
        return 0;
    }
    
    static void readCallback(uv_stream_t* stream, ssize_t nread, const uv_buf_t *buf)
    {
        MasterServerConnection *con = (MasterServerConnection *)stream->data;
        
        if( nread < 0 )
        {
            con->status = MasterServerConnection::Failed;
            return;
        }
        
        http_parser_settings settings = { 0 };
        settings.on_body = readBodyCallback;
        settings.on_message_complete = completeBodyCallback;
        
        ssize_t parsed = http_parser_execute(con->parser, &settings, buf->base, nread);
        
        if(parsed != nread)
        {
            con->status = MasterServerConnection::Failed;
            conoutf(CON_ERROR, "Error parsing the HTTP chunk: %s", http_errno_name(HTTP_PARSER_ERRNO(con->parser)));
            
            //Add \0 terminator
            char *string = newstring(nread);
            strncpy(string, buf->base, nread);
            string[nread+1] = '\0';

            conoutf(CON_ERROR, "Chunk:\n%s", string);
            
            delete[] string;
            
        }

        delete[] buf->base;
    }
    
    static void connectCallback(uv_connect_t* req, int status)
    {
        MasterServerConnection *con = (MasterServerConnection *)req->data;
        
        if(status != 0)
        {
            conoutf(CON_ERROR, "Could not connect to master %s: %s\n", uv_err_name(status), uv_strerror(status));
            con->status = MasterServerConnection::Failed;
        }
        else
        {
            writeGet(con); //Skip connected go into read
            uv_read_start((uv_stream_t *)con->handle, allocCallback, readCallback);
        }
        
        delete req;
    }
    
    static void resolvedMasterCallback(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
    {
        MasterServerConnection *con = (MasterServerConnection *)resolver->data;
        
        if (status < -1)
        {
            conoutf(CON_ERROR, "Could not resolve master host %s: %s\n", uv_err_name(status), uv_strerror(status));
            con->status = MasterServerConnection::Failed;
        }
        else
        {
            con->status = MasterServerConnection::Connecting;
            
            uv_connect_t *req = new uv_connect_t;
            req->data = con;
            
            con->handle = new uv_tcp_t;
            con->handle->data = con;
            uv_tcp_init(con->loop, con->handle);
            
            uv_tcp_connect(req, con->handle, res->ai_addr, connectCallback);
        }
        
        uv_freeaddrinfo(res);
        delete resolver;
    }
    void update()
    {
        if(connection && connection->status == MasterServerConnection::Failed)
        {
            DELETEP(connection);
        }
        
        if(!connection)
        {
            connection = new MasterServerConnection();
            connection->status = MasterServerConnection::Resolving;
            connection->loop = uv_default_loop();
            
            addrinfo *hints = new addrinfo();
            hints->ai_family = PF_INET;
            hints->ai_socktype = SOCK_STREAM;
            hints->ai_protocol = IPPROTO_TCP;
            hints->ai_flags = 0;
            
            defformatstring(port)("%i", masterport);
            defformatstring(name)("%s", mastername);

            conoutf(CON_INFO, "connecting to master at %s:%s", name, port);
            
            uv_getaddrinfo_t *req = new uv_getaddrinfo_t();
            
            req->data = connection;

            int error = 0;
            if ((error = uv_getaddrinfo(connection->loop, req, resolvedMasterCallback, name, port, hints)) < 0)
            {
                conoutf(CON_ERROR, "Could not start resolving the master host: %s: %s\n", uv_err_name(error), uv_strerror(error));
                DELETEP(connection);
                delete req;
            }
        }
        else
        {
            if(connection->status == MasterServerConnection::Connected)
            {
                writeGet(connection);
            }
            
        }
        
        
        
    }
    
    void stop()
    {
        DELETEP(connection);
    }
    
    COMMANDN(updatefrommaster, update, "");
}

namespace serverbrowser
{
    #define find_key(server, keyV) loopv(server->info) if(strcmp(server->info[i].key, keyV) == 0)
    bool sortServers(ServerInfo *&a, ServerInfo *&b)
    {
        int pCountA = -1;
        int pCountB = -1;

        find_key(a, "playercount")
        {
            pCountA = parseint(a->info[i].value);
            break;
        }
        
        find_key(b, "playercount")
        {
            pCountB = parseint(b->info[i].value);
            break;
        }

        if(pCountA != pCountB)
        {
            return pCountB > pCountA;
        }

        enum 
        {
            VERIFIED_NONE = 0,
            VERIFIED_SPONSORED,
            VERIFIED_OFFICIAL,
        };
        
        int verifiedLevelA = VERIFIED_NONE;
        int verifiedLevelB = VERIFIED_NONE;
        
        find_key(a, "name")
        {
            if(NULL != strstr(a->info[i].value, "[Official Server]"))
            {
                verifiedLevelA = VERIFIED_OFFICIAL;
            }
            else if(NULL != strstr(a->info[i].value, "[Sponsored Server]"))
            {
                verifiedLevelA = VERIFIED_SPONSORED;
            }
            break;
        }
        
        find_key(b, "name")
        {
            if(NULL != strstr(b->info[i].value, "[Official Server]"))
            {
                verifiedLevelB = VERIFIED_OFFICIAL;
            }
            else if(NULL != strstr(b->info[i].value, "[Sponsored Server]"))
            {
                verifiedLevelB = VERIFIED_SPONSORED;
            }
            break;
        }
        
        return verifiedLevelB > verifiedLevelA;
    }
    #undef find_key
    
    void loopServers (ident *id, uint *body)
    {
        vector<ServerInfo *> sortedServers;

        enumerate(servers, ServerInfo *, v, {
            sortedServers.add(v);
        });

        sortedServers.sort(sortServers);

        loopstart(id, stack);
        vector<char> buf;
        string value;

        loopv(sortedServers)
        {
            buf.shrink(0);
            loopvj(sortedServers[i]->info)
            {
                formatstring(value)(" %s", escapestring(sortedServers[i]->info[j].key));
                loopk(strlen(value)) buf.add(value[k]);
                formatstring(value)(" %s", escapestring(sortedServers[i]->info[j].value));
                loopk(strlen(value)) buf.add(value[k]);
            }
            buf.add('\0');
            loopiter(id, stack, buf.buf);
            execute(body);
        }

        loopend(id, stack);
    }
    
    COMMAND(loopServers, "re");
    
    bool isLastVersion = true;
    http_parser *lastversionParser = NULL;
    
    static int readBodyVersionCallback(http_parser *parser, const char *buf, size_t len)
    {
        isLastVersion = buf[0] == '1';
        
        return 0;
    }    
    
    static void closeCallback(uv_handle_t *handle)
    {
        delete handle;
    }
    
    static int completeVersionBodyCallback(http_parser *parser)
    {
        uv_close((uv_handle_t *)parser->data,  closeCallback);
        DELETEP(lastversionParser);
        return 0;
    }
    
    static void readVersionCallback(uv_stream_t* stream, ssize_t nread, const uv_buf_t *buf)
    {
        if( nread < 0 )
        {
            uv_close((uv_handle_t *)stream, closeCallback);
            DELETEP(lastversionParser);
            return;
        }

        http_parser_settings settings = { 0 };
        settings.on_body = readBodyVersionCallback;
        settings.on_message_complete = completeVersionBodyCallback;

        ssize_t parsed = http_parser_execute(lastversionParser, &settings, buf->base, nread);

        if(parsed != nread)
        {
            conoutf(CON_ERROR, "Error parsing the HTTP chunk: %s", http_errno_name(HTTP_PARSER_ERRNO(lastversionParser)));

            //Add \0 terminator
            char *string = newstring(nread);
            strncpy(string, buf->base, nread);
            string[nread+1] = '\0';

            conoutf(CON_ERROR, "Chunk:\n%s", string);

            delete[] string;

        }

        delete[] buf->base;
    }
    static void writeVersionGet(uv_stream_t *stream)
    {
        lastversionParser = new http_parser();
        
        string filtered;
        
        //TODO: urlencode
        int i = 0;
        for(const char *c = masterpath;*c;c++)
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
            "GET %s%sversion/check-last?version=%s&protocol=%i HTTP/1.1\r\n"
            "Host: %s\r\n"
            "User-Agent: ReveladeRevolution-client/%s (protocol-%i)\r\n"
            //"Connection: Keep-Alive\r\n"
            "\r\n"
            , filtered, filtered[i-1] == '/' ? "" : "/", version::getVersionString(), server::protocolversion(), mastername
            , version::getVersionString(), server::protocolversion());
        
        uv_buf_t buf;
        buf.len = strlen(cmd);
        buf.base = new char [buf.len];
        strncpy(buf.base, cmd, buf.len);
        
        //Reset
        http_parser_init(lastversionParser, HTTP_RESPONSE);
        lastversionParser->data = stream;
        
        uv_write_t *req = new uv_write_t;
        int err = 0;
        if((err = uv_write(req, stream, &buf, 1, writeCallback)) < 0)
        {
            conoutf(CON_ERROR, "Could not send master request %s: %s\n", uv_err_name(err), uv_strerror(err));
        }
    }
    
    static void connectVersionCallback(uv_connect_t* req, int status)
    {
        if(status < 0)
        {
            conoutf(CON_ERROR, "Could not connect to master %s: %s\n", uv_err_name(status), uv_strerror(status));
        }
        else
        {
            writeVersionGet((uv_stream_t *)req->handle); //Skip connected go into read
            uv_read_start((uv_stream_t *)req->handle, allocCallback, readVersionCallback);
        }
        
        delete req;
    }
    
    static void resolvedVersionMasterCallback(uv_getaddrinfo_t *resolver, int status, struct addrinfo *res)
    {
        if (status < 0)
        {
            conoutf(CON_ERROR, "Could not resolve master host %s: %s\n", uv_err_name(status), uv_strerror(status));
        }
        else
        {
            uv_connect_t *req = new uv_connect_t;

            uv_tcp_t *handle = new uv_tcp_t;
            uv_tcp_init(resolver->loop, handle);

            uv_tcp_connect(req, handle, res->ai_addr, connectVersionCallback);
        }

        uv_freeaddrinfo(res);
        delete resolver;
    }
    
    bool get_isLastVersion()
    {
        static bool requested = false;
        
        if(!requested)
        {
            addrinfo *hints = new addrinfo();
            hints->ai_family = PF_INET;
            hints->ai_socktype = SOCK_STREAM;
            hints->ai_protocol = IPPROTO_TCP;
            hints->ai_flags = 0;
            
            defformatstring(port)("%i", masterport);
            defformatstring(name)("%s", mastername);
            
            conoutf(CON_INFO, "connecting to master at %s:%s", name, port);
            
            uv_getaddrinfo_t *req = new uv_getaddrinfo_t();
            
            int err = 0;
            if ((err = uv_getaddrinfo(uv_default_loop(), req, resolvedVersionMasterCallback, name, port, hints)) < 0)
            {
                conoutf(CON_ERROR, "Could not start resolving the master host %s: %s\n", uv_err_name(err), uv_strerror(err));
                delete req;
            }
            
            requested = true;
        }
        
        return isLastVersion;
    }
    
    ICOMMAND(islastversion, "", (), intret(get_isLastVersion() ? 1 : 0));

    namespace test
    {
        struct CustomProperty
        {
            const char *key;
            const char *value;

            CustomProperty(const char *key, const char *value) : key(key), value(value) {}
            CustomProperty(const char *key, bool value) : key(key), value(newstring(value ? "true" : "false")) {}
            CustomProperty(const char *key, long long value_) : key(key) {
                defformatstring(str)("%lli", value_);
                value = newstring(str);
            }
            CustomProperty(const char *key, long value_) : key(key) {
                defformatstring(str)("%li", value_);
                value = newstring(str);
            }
            CustomProperty(const char *key, int value_) : key(key) {
                defformatstring(str)("%i", value_);
                value = newstring(str);
            }
            CustomProperty(const char *key, short value_) : key(key) {
                defformatstring(str)("%d", value_);
                value = newstring(str);
            }
            CustomProperty(const char *key, float value_) : key(key) {
                defformatstring(str)("%f", value_);
                value = newstring(str);
            }
            CustomProperty(const char *key, double value_) : key(key) {
                defformatstring(str)("%lf", value_);
                value = newstring(str);
            }

            ~CustomProperty()
            {
                DELETEA(key);
                DELETEA(value);
            }
        };

        struct MaintainerInfo
        {
            const char *name;
            bool isAwesome;
            vector<CustomProperty *> customProperties;

            MaintainerInfo() : name(NULL), isAwesome(false) {}
        };

        struct PackageInfo
        {
            const char *name;
            vector<MaintainerInfo *> maintainers;
            int version;

            PackageInfo() : name(NULL), maintainers(), version(-1) {}
        };

        template<typename T>
        void *construct() { return new T; }
        typedef void *(*Constructor)();

        #define memberOffset(struct, member) ((size_t)(&(((struct *)0)->member)))
        #define offsetMember(type, ptr, offset) ((type)((size_t)ptr+offset))

        struct JsonStructInfo
        {
            const char *name;
            enum {
                TYPE_NONE,

                TYPE_STRING,

                TYPE_SHORT,
                TYPE_INT,
                TYPE_LONG,
                TYPE_LONG_LONG,

                TYPE_FLOAT,
                TYPE_DOUBLE,

                TYPE_BOOL,

                TYPE_ARRAY,
                TYPE_MAP,

                TYPE_CUSTOM,
            } type;
            size_t offset;
            JsonStructInfo *children;
            size_t size;
            Constructor constructor;
        };

        #define _(class, type, name, child) { #name, JsonStructInfo::TYPE_##type, memberOffset(class, name), child, 0, NULL},
        JsonStructInfo maintainerInfo[] = {
            _(MaintainerInfo, STRING, name, NULL)
            _(MaintainerInfo, BOOL, isAwesome, NULL)
            {NULL, JsonStructInfo::TYPE_CUSTOM, memberOffset(MaintainerInfo, customProperties), NULL, 0, NULL},
            {NULL, JsonStructInfo::TYPE_NONE}
        };

        JsonStructInfo maintainerInfoContainer = {
            NULL, JsonStructInfo::TYPE_MAP, 0, static_cast<JsonStructInfo *>(maintainerInfo), sizeof(MaintainerInfo), construct<MaintainerInfo>
        };

        JsonStructInfo packageInfoChildren[] = {
            _(PackageInfo, STRING, name, NULL)
            _(PackageInfo,ARRAY, maintainers, static_cast<JsonStructInfo *>(&maintainerInfoContainer))
            _(PackageInfo,INT, version, NULL)
            {NULL, JsonStructInfo::TYPE_NONE}
        };
        #undef _ // Don't be evil

        JsonStructInfo packageInfo = {
            NULL, JsonStructInfo::TYPE_MAP, 0, static_cast<JsonStructInfo *>(packageInfoChildren), sizeof(PackageInfo), construct<PackageInfo>
        };

        struct PackageInfoWorld {
            PackageInfo *info;
        };

        JsonStructInfo packageInfoWorld = {
            NULL, JsonStructInfo::TYPE_MAP, memberOffset(PackageInfoWorld, info), static_cast<JsonStructInfo *>(&packageInfo), 0, NULL
        };

        struct JsonString
        {
            const char *begin;
            size_t length;
            JsonString() : begin(NULL), length(0) {}
            void set(const char *begin, size_t length)
            {
                this->begin = begin;
                this->length = length;
            }

            void clear()
            {
                begin = NULL;
                length = 0;
            }
        };

        struct ReadingElement
        {
            JsonStructInfo *currentInfo;
            ReadingElement *parent;
            void *map;

            ReadingElement(JsonStructInfo *currentInfo, ReadingElement *parent, void *map) : currentInfo(currentInfo), parent(parent), map(map) {}
        };

        struct ReadingContext
        {
            JsonString key;
            ReadingElement *readingElement;
            int ignoreStack;
            char *errorMessage;

            ReadingContext() : key(), readingElement(NULL), ignoreStack(0), errorMessage(NULL) {}
            ~ReadingContext()
            {
                DELETEA(errorMessage);
            }

            void popElement()
            {
                ReadingElement *parent = readingElement->parent;
                delete readingElement;
                readingElement = parent;
            }
        };

        struct PackageInfoReadingContext : ReadingContext
        {
            PackageInfoReadingContext(PackageInfoWorld *world)
            {
                readingElement = new ReadingElement(&packageInfoWorld, NULL, world);
            }
        };

        // Find a valid value
        JsonStructInfo *getInfo(JsonStructInfo *info, const JsonString &key, int type)
        {
            if(info->children)
            {
                for(JsonStructInfo *child = info->children; child->type != JsonStructInfo::TYPE_NONE; child++)
                {
                    if((!child->name || 0 == strncmp(child->name, key.begin, key.length)) && (child->type == type || (child->type == JsonStructInfo::TYPE_CUSTOM && type != JsonStructInfo::TYPE_MAP && type != JsonStructInfo::TYPE_ARRAY)))
                    {
                        return child;
                    }
                }
            }

            return NULL;
        }

        template<typename T>
        void insertInto(ReadingElement *parent, JsonStructInfo *info, T value, const JsonString &key)
        {
            if(parent->currentInfo->type == JsonStructInfo::TYPE_MAP)
            {
                if(info->type == JsonStructInfo::TYPE_CUSTOM)
                {
                    offsetMember(vector<CustomProperty *> *, parent->map, info->offset)->add(new CustomProperty(
                        key.begin ? newstring(key.begin, key.length) : NULL,
                        value
                    ));
                }
                else
                {
                    *offsetMember(T *, parent->map, info->offset) = value;
                }
            }
            else // Array
            {
                ASSERT(info->type != JsonStructInfo::TYPE_CUSTOM);
                ((vector<T> *)parent->map)->add(value);
            }
        }

        #define INIT_CONTEXT ReadingContext *context = (ReadingContext *)context_;
        #define EXIT_CONTEXT \
            if(info == NULL && context->ignoreStack < 1) { \
                ASSERT(context->errorMessage == NULL); \
                DELETEA(context->errorMessage); \
                context->errorMessage = newstring("Key/type mismatch."); \
            } \
            return info == NULL && context->ignoreStack < 1 ? 0 : 1;
        int json_start_map(void *context_)
        {
            INIT_CONTEXT
            JsonStructInfo *info = getInfo(context->readingElement->currentInfo, context->key, JsonStructInfo::TYPE_MAP);
            if(context->ignoreStack || info == NULL) {
                context->key.clear();
                context->ignoreStack++;
                EXIT_CONTEXT
            } // Ignore the map if it's invalid

            context->key.clear();

            void *map = NULL;
            if(info->constructor) map = info->constructor();
            else
            {
                ASSERT(map);
                memset(map, 0, info->size);
                map = malloc(info->size);
            }
            context->readingElement = new ReadingElement(info, context->readingElement, map);
            insertInto(context->readingElement->parent, info, map, context->key);
            EXIT_CONTEXT
        }

        int json_end_map(void *context_)
        {
            INIT_CONTEXT
            if(context->ignoreStack)
            {
                context->ignoreStack--;
            }
            else
            {
                context->popElement();
            }
            return 1;
        }

        int json_start_array(void *context_)
        {
            INIT_CONTEXT
            JsonStructInfo *info = getInfo(context->readingElement->currentInfo, context->key, JsonStructInfo::TYPE_ARRAY);
            if(context->ignoreStack || info == NULL) {
                context->key.clear();
                context->ignoreStack++;
                EXIT_CONTEXT
            } // Ignore the array if it's invalid

            context->key.clear();

            context->readingElement = new ReadingElement(
                info,
                context->readingElement,
                offsetMember(void *, context->readingElement->map, info->offset)
            );

            EXIT_CONTEXT
        }

        int json_end_array(void *context_)
        {
            INIT_CONTEXT
            if(context->ignoreStack)
            {
                context->ignoreStack--;
            }
            else
            {
                context->popElement();
            }
            return 1;
        }

        int json_read_string(void *context_, const unsigned char *value, size_t length)
        {
            INIT_CONTEXT
            JsonStructInfo *info = getInfo(context->readingElement->currentInfo, context->key, JsonStructInfo::TYPE_STRING);
            if(info)
            {
                const char *v = newstring((const char *)value, length);
                insertInto(context->readingElement, info, v, context->key);
            }
            context->key.clear();
            EXIT_CONTEXT
        }

        template <typename T, typename U>
        bool doesFit(const U value)
        {
            return ((value>U(0))==(T(value)>T(0))) && U(T(value))==value;
        }

        #define TRY(type) if(info || i == 1) { insertInto(context->readingElement, info, (type)value, context->key); break; }
        int json_read_int(void *context_, long long value)
        {
            INIT_CONTEXT
            JsonStructInfo *info = NULL;

            // Try best match first, then try any
            loopi(2)
            {
                if(doesFit<short>(value)) info = getInfo(context->readingElement->currentInfo, context->key, JsonStructInfo::TYPE_SHORT);
                TRY(short)
                if(doesFit<int>(value)) info = getInfo(context->readingElement->currentInfo, context->key, JsonStructInfo::TYPE_INT);
                TRY(int)
                if(doesFit<long>(value)) info = getInfo(context->readingElement->currentInfo, context->key, JsonStructInfo::TYPE_LONG);
                TRY(long)
                info = getInfo(context->readingElement->currentInfo, context->key, JsonStructInfo::TYPE_LONG_LONG);
                TRY(long long)
            }
            context->key.clear();
            EXIT_CONTEXT
        }

        int json_read_float(void *context_, double value)
        {
            INIT_CONTEXT

            JsonStructInfo *info = NULL;
            loopi(2)
            {
                if(i == 1 || (float)value == value) info = getInfo(context->readingElement->currentInfo, context->key, JsonStructInfo::TYPE_FLOAT);
                TRY(float)
                info = getInfo(context->readingElement->currentInfo, context->key, JsonStructInfo::TYPE_DOUBLE);
                TRY(double)
            }

            context->key.clear();
            EXIT_CONTEXT
        }
        #undef TRY

        int json_read_bool(void *context_, int value)
        {
            INIT_CONTEXT
            JsonStructInfo *info = getInfo(context->readingElement->currentInfo, context->key, JsonStructInfo::TYPE_BOOL);
            if(info)
            {
                insertInto(context->readingElement, info, value && true, context->key);
            }
            context->key.clear();
            EXIT_CONTEXT
        }

        int json_read_key(void *context_, const unsigned char *string, size_t length)
        {
            INIT_CONTEXT
            context->key.set((const char *)string, length);
            return 1;
        }

        static yajl_callbacks lyajl_callbacks = {
            NULL, json_read_bool,
            json_read_int, json_read_float, NULL,
            json_read_string,
            json_start_map, json_read_key, json_end_map,
            json_start_array, json_end_array
        };

        void test()
        {
            PackageInfoWorld world = {0};
            PackageInfoReadingContext *context = new PackageInfoReadingContext(&world);
            yajl_handle a = yajl_alloc(&lyajl_callbacks, NULL, context);
            const char *buffer = "{\"name\":\"hello-world\",\"maintainers\":[{\"name\":\"killme\",\"isAwesome\":true,\"petName\":\"naga\"},{\"name\":\"Bumi\"}]}";
            yajl_status status = yajl_parse(a, (const unsigned char*)buffer, strlen(buffer));
            if(status == yajl_status_ok) status = yajl_complete_parse(a);
            if (status != yajl_status_ok)
            {
                unsigned char * str = yajl_get_error(a, 1, (const unsigned char*)buffer, strlen(buffer));
                conoutf(CON_ERROR, "Json parser error (%s): %s", context->errorMessage, str);

                yajl_free_error(a, str);
            }
            DELETEA(context->errorMessage);

            printf("name=%s\n", world.info->name);
            loopv(world.info->maintainers){
                printf("maintainers[%i]={name:%s,isAwesome:%i}\n", i, world.info->maintainers[i]->name, world.info->maintainers[i]->isAwesome);
                loopvj(world.info->maintainers[i]->customProperties)
                {
                    printf("maintainers[%i].customProperties[%i]={name:%s,value:%s}\n", i, j,
                        world.info->maintainers[i]->customProperties[j]->key,
                        world.info->maintainers[i]->customProperties[j]->value);
                }
            }

            ASSERT(0);
        };

        //COMMAND(test, "");
    }
}
