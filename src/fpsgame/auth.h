namespace auth
{
    extern void getRandom(ustring *str);

    extern bool verifyCertificateWithCA(stream *cert);
    extern bool verifyNotInCRL(stream *cert);

    extern bool encryptWithPublicCert(stream *cert, const uchar *data, int dataLength, uchar **out, int *outSize);
    extern bool decryptWithPrivateCert(stream *cert, const uchar *data, int dataLength, uchar **out, int *outSize);

    extern void init();

    //The public certificate of the client or the server
#ifndef STANDALONE
    extern stream *clientCertificate;
    extern stream *clientCertificatePrivate;
#endif
    extern stream *serverCertificate;
    extern stream *serverCertificatePrivate;

    //The public certificate of the certificate authority
    //extern stream *CACertfificate;
}
