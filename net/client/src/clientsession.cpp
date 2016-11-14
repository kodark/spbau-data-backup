#include "clientsession.h"
#include <archiver.h>
#include <fstream>

#include <iostream>
#include <cstring>
#include <string>
#include <time.h>
#include <vector>

#include <QFile>
#include <QFileInfo>
#include <QByteArray>


#define DEBUG_FLAG true
#define LOG(format, var) do{ \
    if (DEBUG_FLAG) fprintf(stderr, "line: %d.  " format, __LINE__ ,var); \
    }while(0)

ClientSession::ClientSession(NetworkStream* networkStream, ConsoleStream* consoleStream, QObject *parent)
    :QObject(parent)
    ,mNetworkStream(networkStream)
    ,mConsoleStream(consoleStream)
    ,mClientState(NOT_STARTED) {
    consoleStream->setParent(this);
    networkStream->setParent(this);
    connect(mConsoleStream, &ConsoleStream::readln, this, &ClientSession::onConsoleInput);
    connect(this, &ClientSession::sigWriteToConsole, mConsoleStream, &ConsoleStream::println);
    connect(mNetworkStream,  &NetworkStream::newNetMessage, this, &ClientSession::onNetworkInput);
    connect(this, &ClientSession::sigWriteToNetwork, mNetworkStream, &NetworkStream::sendNetMessage);
    connect(mNetworkStream, &NetworkStream::connected, this, &ClientSession::onStart);
}

void ClientSession::onConsoleInput(const std::string& message) {
    if (mClientState == ABORTED) {
        std::cerr << "Current state - ABORTED. Unexpected user input.";
        return;
    }

    if (mClientState == WAIT_LOGIN_INPUT) {
        sendLoginRequest(message);
        return;
    }

    if (mClientState != WAIT_USER_INPUT) {
        emit sigWriteToConsole("Busy. Try later.");
        return;
    }


    if (message == "exit")
        exit(0);
    if (message == "help") {
        emit sigWriteToConsole("Commands:\n"
                               "ls [backupId] -- list all backups shortly or all info about one backup.\n"
                               "restore backupId path -- download backup by backupId and restore it in path.\n"
                               "backup path -- make backup");

    }

    else if (message == "ls") {
        askLs();
    } else if (message.substr(0, 2) == "ls") {
        askLs(message.substr(3));
    } else if (message.substr(0, 7) == "restore") {
        makeRestoreRequest(message.substr(8));
    } else if (message.substr(0, 6) == "backup") {
        makeBackup(message.substr(7));
    } else {
        emit sigWriteToConsole("Unknown command: " + message + "\nType \"help\" to show available commands.");
    }
}


void ClientSession::onNetworkInput(const QByteArray &message) {
    utils::commandType cmd = *((utils::commandType*)message.data());
    switch (cmd) {
    case utils::ansToClientLogin:
        OnLoginAns(message.data()+utils::commandSize, message.size()-utils::commandSize);
        break;
    case utils::ansToLsSummary:
        OnSummaryLs(message.data()+utils::commandSize, message.size()-utils::commandSize);
        break;
    case utils::ansToLsDetailed:
        OnDetailedLs(message.data()+utils::commandSize, message.size()-utils::commandSize);
        break;
    case utils::ansToBackup:
        OnReceiveBackupResults(message.data()+utils::commandSize, message.size()-utils::commandSize);
        break;
    case utils::ansToRestore:
        OnReceiveArchiveToRestore(message.data()+utils::commandSize, message.size()-utils::commandSize);
        break;
    case utils::serverExit:
        OnServerError(message.data()+utils::commandSize, message.size()-utils::commandSize);
        break;
    case utils::notFoundBackupByIdOnServer:
        OnNotFoundBackupId(message.data()+utils::commandSize, message.size()-utils::commandSize);
        break;
    case utils::serverError:
        OnServerExit(message.data()+utils::commandSize, message.size()-utils::commandSize);
        break;
    default:
        std::cerr << "Unsupported command from server." << std::endl;
    }
}


void ClientSession::onStart() {
    if (mClientState == NOT_STARTED) {
        mClientState = WAIT_LOGIN_INPUT;
        emit sigWriteToConsole("Print login:");
    } else {
        mClientState = ABORTED;
        std::cerr << "Error, connecting after connected..." << std::endl;
    }
}

void ClientSession::sendLoginRequest(const std::string& login) {
    networkUtils::protobufStructs::LoginClientRequest loginRequest;
    loginRequest.set_login(login);

    std::string serializatedLoginRequest;
    if (!loginRequest.SerializeToString(&serializatedLoginRequest)) {
        std::cerr << "Error with serialization login request cmd." << std::endl;
        return;
    }
    sendSerializatedMessage(serializatedLoginRequest, utils::clientLogin, loginRequest.ByteSize());
    mClientState = WAIT_LOGIN_STATUS;
}

void ClientSession::sendLsFromClient(networkUtils::protobufStructs::LsClientRequest ls) {
    std::string serializationLs;
    if (!ls.SerializeToString(&serializationLs)) {
        std::cerr << "Error with serialization LS cmd." << std::endl;
        return;
    }
    sendSerializatedMessage(serializationLs, utils::ls, ls.ByteSize());
}

void ClientSession::askLs() {
    networkUtils::protobufStructs::LsClientRequest ls;
    ls.set_isls(1);
    sendLsFromClient(ls);
    mClientState = WAIT_LS_RESULT;
}

void ClientSession::askLs(const std::string& command) {
    networkUtils::protobufStructs::LsClientRequest ls;
    ls.set_isls(1);
    std::uint64_t backupId = 0;
    // XXX sscanf
    for (size_t i = 0; i < command.size(); ++i) {
        if (command[i] >= '0' && command[i] <= '9') {
            backupId = 10 * backupId + (command[i]-'0');
        }
        else
            break;
    }
    ls.set_backupid(backupId);
    sendLsFromClient(ls);
    mClientState = WAIT_LS_RESULT;
}

void ClientSession::makeRestoreRequest(const std::string& command) {
    networkUtils::protobufStructs::RestoreClientRequest restoreRequest;
    std::uint64_t backupId = 0;
    size_t i = 0;
    // XXX sscanf
    for (i = 0; i < command.size(); ++i) {
        if (command[i] >= '0' && command[i] <= '9') {
            backupId = 10 * backupId + (command[i]-'0');
        }
        else
            break;
    }
    ++i;
    restoreRequest.set_backupid(backupId);

    if (i >= command.size()) {
        std::cerr << "Missing path in restore request" << std::endl;
        return;
    }

    restorePath = command.substr(i);

    std::string serializatedRestoreRequest;
    if (!restoreRequest.SerializeToString(&serializatedRestoreRequest)) {
        std::cerr << "Error with serialization restore request cmd." << std::endl;
        return;
    }
    sendSerializatedMessage(serializatedRestoreRequest, utils::restore, restoreRequest.ByteSize());
    mClientState = WAIT_RESTORE_RESULT;
    LOG("Send restore request. Size = %d\n", restoreRequest.ByteSize());
}

void ClientSession::makeBackup(const std::string& command) {
    networkUtils::protobufStructs::ClientBackupRequest backupRequest;
    Archiver::pack(command.c_str(), "curpack.pck");
    QFile archive("curpack.pck");
    archive.open(QIODevice::ReadOnly);
    QByteArray archiveByte = archive.readAll();
    backupRequest.set_archive(archiveByte.data(), archiveByte.size());
    backupRequest.set_path(command);

    std::string serializatedBackupRequest;
    if (!backupRequest.SerializeToString(&serializatedBackupRequest)) {
        std::cerr << "Error with serialization backup request cmd." << std::endl;
        return;
    }
    sendSerializatedMessage(serializatedBackupRequest, utils::backup, backupRequest.ByteSize());

    LOG("archivesize = %ld\n", backupRequest.archive().size());

    mClientState = WAIT_BACKUP_RESULT;
}

void ClientSession::sendRestoreResult(bool restoreResult) {
    networkUtils::protobufStructs::ClientReplyAfterRestore reply;
    reply.set_issuccess(restoreResult);

    std::string serializedReply;
    if (!reply.SerializeToString(&serializedReply)) {
        std::cerr << "Error with serialization backup request cmd." << std::endl;
        return;
    }
    sendSerializatedMessage(serializedReply, utils::replyAfterRestore, reply.ByteSize());
    mClientState = WAIT_USER_INPUT;
    LOG("Send replyAfterRestore. Size = %d\n", reply.ByteSize());
}

// XXX sprintf
std::string inttostr(std::uint64_t number) {
    if (number == 0)
        return "0";
    std::string S = "";
    while (number > 0) {
        S = std::string(1, '0'+(char)(number % 10)) + S;
        number /= 10;
    }
    return S;
}

void ClientSession::OnLoginAns(const char *buffer, uint64_t bufferSize) {
    if (mClientState != WAIT_LOGIN_STATUS) {
        std::cerr << "Unexpected LoginAns" << std::endl;
        mClientState = ABORTED;
        return;
    }

    networkUtils::protobufStructs::LoginServerAnswer serverAnswer;
    serverAnswer.ParseFromArray(buffer, bufferSize);
    if (serverAnswer.issuccess()) {
        mClientState = WAIT_USER_INPUT;
        emit sigWriteToConsole("Login was success. Print command or \"exit\" to close app:");
    } else {
        mClientState = WAIT_LOGIN_INPUT;
        emit sigWriteToConsole("Login was unsuccess. Print login:");
    }
}

std::string shortBackupInfoToString(const networkUtils::protobufStructs::ShortBackupInfo& backupInfo) {
    time_t time = (time_t)backupInfo.time();
    return inttostr(backupInfo.backupid()) + " " + backupInfo.path() + " " + ctime(&time);
}

void ClientSession::OnDetailedLs(const char *buffer, uint64_t bufferSize) {
    if (mClientState != WAIT_LS_RESULT) {
        std::cerr << "Unexpected DETAILED_LS_RESULT" << std::endl;
        mClientState = ABORTED;
        return;
    }
    networkUtils::protobufStructs::LsDetailedServerAnswer lsDetailed;
    lsDetailed.ParseFromArray(buffer, bufferSize);

    QFile meta("curpack.pck");
    meta.open(QIODevice::WriteOnly);
    meta.write(lsDetailed.meta().c_str(), lsDetailed.meta().size());
    meta.close();
    LOG("lsDetailed.meta().size() = %ld\n", lsDetailed.meta().size());

    QString fsTree;

    QTextStream qTextStream(&fsTree,  QIODevice::WriteOnly);
    Archiver::printArchiveFsTree("curpack.pck", qTextStream);
    qTextStream.flush();

    LOG("fsTree.size() = %d\n", fsTree.size());

    std::string temp = shortBackupInfoToString(lsDetailed.shortbackupinfo()) + "\n" + fsTree.toStdString();
    emit sigWriteToConsole(temp);

    mClientState = WAIT_USER_INPUT;
}

void ClientSession::OnSummaryLs(const char *buffer, uint64_t bufferSize) {
    if (mClientState != WAIT_LS_RESULT) {
        std::cerr << "Unexpected SUMMURY_LS_RESULT" << std::endl;
        mClientState = ABORTED;
        return;
    }

    networkUtils::protobufStructs::LsSummaryServerAnswer lsSummary;
    lsSummary.ParseFromArray(buffer, bufferSize);

    std::string ans;
    for (int i = 0; i < lsSummary.shortbackupinfo_size(); ++i) {
        ans += shortBackupInfoToString(lsSummary.shortbackupinfo(i));
    }
    ans += inttostr(lsSummary.shortbackupinfo_size()) + " backups.";
    emit sigWriteToConsole(ans);
    mClientState = WAIT_USER_INPUT;
}

void ClientSession::OnReceiveArchiveToRestore(const char *buffer, uint64_t bufferSize) {
    if (mClientState != WAIT_RESTORE_RESULT) {
        std::cerr << "Unexpected RESTORE_RESULT" << std::endl;
        mClientState = ABORTED;
        return;
    }

    networkUtils::protobufStructs::RestoreServerAnswer restoreAnswer;
    restoreAnswer.ParseFromArray(buffer, bufferSize);
    LOG("Receive restore. Size = %d\n", restoreAnswer.ByteSize());
    LOG("archivesize = %ld\n", restoreAnswer.archive().size());

    QFile archive("curunpack.pck");
    archive.open(QIODevice::WriteOnly);
    archive.write(restoreAnswer.archive().c_str(), restoreAnswer.archive().size());
    archive.close();

    LOG("restorePath: %s\n", restorePath.c_str());
    QFileInfo qfileinfo(restorePath.c_str());
    LOG("Absolute restorePath: %s\n", qfileinfo.canonicalFilePath().toStdString().c_str());
    try {
        if (qfileinfo.isDir()) {
            Archiver::unpack("curunpack.pck", restorePath.c_str());
            sendRestoreResult(1);
        } else {
            std::cerr << restorePath << " isn't directory." << std::endl;
            sendRestoreResult(0);
        }
    } catch (Archiver::ArchiverException e) {
        std::cerr << e.whatQMsg().toStdString() << std::endl;
        sendRestoreResult(0);
    }

    mClientState = WAIT_USER_INPUT;
}

void ClientSession::OnNotFoundBackupId(const char *buffer, uint64_t bufferSize) {
    networkUtils::protobufStructs::ServerError serverError;
    serverError.ParseFromArray(buffer, bufferSize);

    emit sigWriteToConsole(serverError.errormessage());

    mClientState = WAIT_USER_INPUT;
}


void ClientSession::OnReceiveBackupResults(const char *buffer, uint64_t bufferSize) {
    if (mClientState != WAIT_BACKUP_RESULT) {
        std::cerr << "Unexpected BACKUP_RESULT" << std::endl;
        mClientState = ABORTED;
        return;
    }

    networkUtils::protobufStructs::ServerBackupResult backupResult;
    backupResult.ParseFromArray(buffer, bufferSize);
    if (backupResult.issuccess()) {
        emit sigWriteToConsole("Backup was succed. BackupId = " + inttostr(backupResult.backupid()));
    } else {
        emit sigWriteToConsole("Backup wasn't succed.");
    }
    mClientState = WAIT_USER_INPUT;
}

void ClientSession::OnServerError(const char *buffer, uint64_t bufferSize) {
    networkUtils::protobufStructs::ServerError serverError;
    serverError.ParseFromArray(buffer, bufferSize);
    emit sigWriteToConsole("Server error: " + serverError.errormessage());
    mClientState = WAIT_USER_INPUT;
}

void ClientSession::OnServerExit(const char *buffer, uint64_t bufferSize) {
    networkUtils::protobufStructs::ServerError serverError;
    serverError.ParseFromArray(buffer, bufferSize);
    emit sigWriteToConsole("Server error: " + serverError.errormessage() + "\nState is aborted.");
    mClientState = ABORTED;
}

void ClientSession::sendSerializatedMessage(const std::string& binaryMessage, utils::commandType cmdType, int messageSize) {
    std::vector<char> message(messageSize + utils::commandSize);
    std::memcpy(message.data(), (char*)&cmdType, utils::commandSize);
    std::memcpy(message.data() + utils::commandSize, binaryMessage.c_str(), messageSize);
    emit sigWriteToNetwork(QByteArray(message.data(), messageSize + utils::commandSize));
}
