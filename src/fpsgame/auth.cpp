#include "game.h"
#include "auth.h"

namespace auth
{
    void getRandom(ustring *str)
    {
        ustring &s = *str;
        copystring((char *)s, "0123456789abcdefghijklmnopqrstuvwxyz");
    }

    bool verifyCertificateWithCA(stream* cert)
    {
        return true;
    }

    bool verifyNotInCRL(stream* cert)
    {
        return true;
    }

    bool encryptWithPublicCert(stream *cert, const uchar *data, int dataLength, uchar **out, int *outSize)
    {
        //TODO: allocation free version using outsize
        //TODO: actually encrypt data
        *out = new uchar [dataLength];
        memcpy(*out, data, dataLength);
        *outSize = dataLength;
        return true;
    }

    bool decryptWithPrivateCert(stream *cert, const uchar *data, int dataLength, uchar **out, int *outSize)
    {
        //TODO: allocation free version using outsize
        //TODO: actually decrypt data
        *out = new uchar [dataLength];
        memcpy(*out, data, dataLength);
        *outSize = dataLength;
        return true;
    }
    
    void test()
    {
        ustring str;
        getRandom(&str);
        uchar *data = NULL; int dataLength = 0;
        encryptWithPublicCert(serverCertificate, str, sizeof(str), &data, &dataLength);
        uchar *dataOut = NULL; int dataOutLength = 0;
        decryptWithPrivateCert(serverCertificatePrivate, data, dataLength, &dataOut, &dataOutLength);
        abort();
    }

    void init()
    {
#if 0
        static bool didInit = false;
        if(didInit) return; didInit = true;
        stream *s = opentempfile("auth-dummy", "w+b");
        s->write("DUMMY DATA", 9);
#ifndef STANDALONE
        clientCertificate = s;
        clientCertificatePrivate = s;
#endif
        serverCertificate = s;
        serverCertificatePrivate = s;

        //test();
#endif
    }

    //The public certificate of the client or the server
    #ifndef STANDALONE
    stream *clientCertificate = NULL;
    stream *clientCertificatePrivate = NULL;
    #endif
    stream *serverCertificate = NULL;
    stream *serverCertificatePrivate = NULL;

    //The public certificate of the certificate authority
    //stream *CACertfificate = DUMMY_STREAM;
}