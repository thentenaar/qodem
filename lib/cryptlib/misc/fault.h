/****************************************************************************
*																			*
*						cryptlib Fault-Injection Header File 				*
*						Copyright Peter Gutmann 1998-2013					*
*																			*
****************************************************************************/

#ifndef _FAULT_DEFINED

#define _FAULT_DEFINED

/* In the debug build, the code includes additional functionality to inject 
   faults that verify that possible conditions such as invalid signatures 
   are correctly detected.  The following values are check types that
   can be performed for the higher-level envelope and session objects */

#ifndef NDEBUG

/****************************************************************************
*																			*
*							Fault Types and Handling						*
*																			*
****************************************************************************/

/* The various fault types that we can inject */

typedef enum {
	FAULT_NONE,					/* No fault type */
	FAULT_SESSION_CORRUPT_HANDSHAKE, /* Corruption/manip.of handshake messages */
	FAULT_SESSION_CORRUPT_DATA,	/* Corruption/manip.of data exchange */
	FAULT_SESSION_WRONGCERT,	/* Wrong cert.for URL/email address */
	FAULT_SESSION_BADSIG_HASH,	/* Sig.check failure - bad hash */
	FAULT_SESSION_BADSIG_DATA,	/* Sig.check failure - bad data */
	FAULT_LAST					/* Last possible fault type */
	} FAULT_TYPE;

/* The handler for injecting faults.  This would be called as:

	INJECT_FAULT( SESSION_WRONGCERT, SESSION_WRONGCERT_1 ); */

#define INJECT_FAULT( type, action ) \
		if( faultType == ( FAULT_##type ) ) \
			{ \
			FAULTACTION_##action; \
			}

/****************************************************************************
*																			*
*								Session Faults								*
*																			*
****************************************************************************/

/* SSH session fault types */

/* Corrupt the server hello to make sure that it's detected by the other 
   side */

#define FAULTACTION_SESSION_CORRUPT_HANDSHAKE_SSH_1 \
	if( cryptStatusOK( status ) ) \
		{ \
		( ( BYTE * ) serverHelloPtr )[ serverHelloLength - 1 ]++; \
		}
#define FAULTACTION_SESSION_CORRUPT_HANDSHAKE_SSH_2 \
	( ( BYTE * ) serverHelloPtr )[ serverHelloLength - 1 ]--;

/* Corrupt the payload data to make sure that it's detected by the other 
   side */

#define FAULTACTION_SESSION_CORRUPT_DATA_SSH_1 \
	if( isServer( sessionInfoPtr ) ) \
		{ \
		sessionInfoPtr->sendBuffer[ SSH2_HEADER_SIZE + \
									SSH2_PAYLOAD_HEADER_SIZE + 4 ]++; \
		}

/* Swap the server key for a different one to make sure that it's detected 
   by the other side */

#define FAULTACTION_SESSION_WRONGCERT_SSH_1 \
	faultParam1 = sessionInfoPtr->privateKey; \
	status = getSubstituteKey( &sessionInfoPtr->privateKey ); \
	if( cryptStatusError( status ) ) \
		retIntError();
#define FAULTACTION_SESSION_WRONGCERT_SSH_2 \
	krnlSendNotifier( sessionInfoPtr->privateKey, IMESSAGE_DESTROY ); \
	sessionInfoPtr->privateKey = faultParam1;

/* Corrupt the keyex hash that's signed and sent to the client to make sure 
   that it's detected by the other side */

#define FAULTACTION_SESSION_BADSIG_HASH_SSH_1 \
	if( isServer ) \
		{ \
		krnlSendMessage( handshakeInfo->iExchangeHashContext, \
						 IMESSAGE_CTX_HASH, "x", 1 ); \
		}

/* Corrupt the server keyex value that's sent to the client to make sure 
   that it's detected by the other side */

#define FAULTACTION_SESSION_BADSIG_DATA_SSH_1 \
	handshakeInfo->serverKeyexValue[ 8 ]++;
#define FAULTACTION_SESSION_BADSIG_DATA_SSH_2 \
	handshakeInfo->serverKeyexValue[ 8 ]--;

/* SSL session fault types */

/* Corrupt the session ID to make sure that it's detected by the other side 
   (this actually gets detected by us first since the client sends its 
   Finished before we do, and the MAC in the Finished doesn't match what 
   we're expecting).  The session ID is arbitrary and not used in any crypto 
   calculations, which makes it safe to corrupt since it has to be detected 
   entirely by a MAC failure */

#define FAULTACTION_SESSION_CORRUPT_HANDSHAKE_SSL_1 \
	faultParam1 = ( stell( stream ) - handshakeInfo->sessionIDlength ) + 4; \
	sessionInfoPtr->sendBuffer[ faultParam1 ]++;
#define FAULTACTION_SESSION_CORRUPT_HANDSHAKE_SSL_2 \
	if( cryptStatusOK( status ) ) \
		sessionInfoPtr->sendBuffer[ faultParam1  ]--;

/* Corrupt the payload data to make sure that it's detected by the other 
   side */

#define FAULTACTION_SESSION_CORRUPT_DATA_SSL_1 \
	if( !cryptStatusError( status ) ) \
		sessionInfoPtr->sendBuffer[ SSL_HEADER_SIZE + 4 ]++; 

/* Swap the server certificate for a different one to make sure that it's 
   detected by the other side */

#define FAULTACTION_SESSION_WRONGCERT_SSL_1 \
	faultParam1 = sessionInfoPtr->privateKey; \
	status = getSubstituteKey( &sessionInfoPtr->privateKey ); \
	if( cryptStatusError( status ) ) \
		retIntError();
#define FAULTACTION_SESSION_WRONGCERT_SSL_2 \
	krnlSendNotifier( sessionInfoPtr->privateKey, IMESSAGE_DESTROY ); \
	sessionInfoPtr->privateKey = faultParam1;

/* Corrupt the hash value to make sure that it's detected by the other 
   side */

#define FAULTACTION_SESSION_BADSIG_HASH_SSL_1 \
	krnlSendMessage( shaContext, IMESSAGE_DELETEATTRIBUTE, NULL, \
					 CRYPT_CTXINFO_HASHVALUE ); \
	status = krnlSendMessage( shaContext, IMESSAGE_CTX_HASH, \
							  ( MESSAGE_CAST ) keyData, keyDataLength ); \
	if( cryptStatusOK( status ) ) \
		status = krnlSendMessage( shaContext, IMESSAGE_CTX_HASH, "", 0 ); \
	if( cryptStatusError( status ) ) \
		retIntError();

/* Corrupt the server keyex value to make sure that it's detected by the 
   other side */

#define FAULTACTION_SESSION_BADSIG_DATA_SSL_1 \
	( ( BYTE * ) keyData )[ 16 ]++;
#define FAULTACTION_SESSION_BADSIG_DATA_SSL_2 \
	( ( BYTE * ) keyData )[ 16 ]--;

/****************************************************************************
*																			*
*					Fault-Injection Variables and Functions					*
*																			*
****************************************************************************/

/* The global value that records the fault type to inject.  In addition to
   this we may need to store parameter information relating to the fault,
   the faultParam values provide for this storage */

extern FAULT_TYPE faultType;
extern int faultParam1;

/* Get a substitute key to replace the actual one, used to check for 
   detection of use of the wrong key */

CHECK_RETVAL STDC_NONNULL_ARG( ( 1 ) ) \
int getSubstituteKey( OUT_HANDLE_OPT CRYPT_CONTEXT *iPrivateKey );

#else

/****************************************************************************
*																			*
*					Release-Build Removes Fault Injection					*
*																			*
****************************************************************************/

#define INJECT_FAULT( type, action )

#endif /* !NDEBUG */
#endif /* _FAULT_DEFINED */
