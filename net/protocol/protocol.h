#ifndef _PROTOCOL_
#define _PROTOCOL_

#include <stdint.h>

#define PORT_NUMBER 12344
#define BUF_SIZE 100

namespace utils{
    const int commandSize = sizeof(uint8_t);
    typedef uint8_t commandType;

    enum ClientCommandId{
        ls,
        restore,
        replyAfterRestore,
        backup,
        clientExit
    };

    enum ServerCommandId{
        ansToLsSummary,
        ansToLsDetailed,
        ansToRestore,
        ansToBackup,
        serverExit,
        notFoundBackupByIdOnServer
    };

    inline commandType toFixedType(ClientCommandId cmd){
        return cmd;
    }

    inline uint8_t toFixedType(ServerCommandId cmd){
        return cmd;
    }
}


#endif
