# vim: ft=bzl

cc_library(
    name = "wangle",
    srcs = [
        "wangle/acceptor/Acceptor.cpp",
        "wangle/acceptor/AcceptorHandshakeManager.cpp",
        "wangle/acceptor/ConnectionManager.cpp",
        "wangle/acceptor/EvbHandshakeHelper.cpp",
        "wangle/acceptor/FizzAcceptorHandshakeHelper.cpp",
        "wangle/acceptor/FizzConfigUtil.cpp",
        "wangle/acceptor/LoadShedConfiguration.cpp",
        "wangle/acceptor/ManagedConnection.cpp",
        "wangle/acceptor/SSLAcceptorHandshakeHelper.cpp",
        "wangle/acceptor/SecureTransportType.cpp",
        "wangle/acceptor/SocketOptions.cpp",
        "wangle/acceptor/TLSPlaintextPeekingCallback.cpp",
        "wangle/acceptor/TransportInfo.cpp",
        "wangle/bootstrap/ServerBootstrap.cpp",
        "wangle/channel/FileRegion.cpp",
        "wangle/channel/Pipeline.cpp",
        "wangle/client/persistence/FilePersistenceLayer.cpp",
        "wangle/client/persistence/PersistentCacheCommon.cpp",
        "wangle/client/ssl/SSLSessionCacheData.cpp",
        "wangle/client/ssl/SSLSessionCacheUtils.cpp",
        "wangle/client/ssl/SSLSessionCallbacks.cpp",
        "wangle/codec/LengthFieldBasedFrameDecoder.cpp",
        "wangle/codec/LengthFieldPrepender.cpp",
        "wangle/codec/LineBasedFrameDecoder.cpp",
        "wangle/ssl/PasswordInFileFactory.cpp",
        "wangle/ssl/SSLContextManager.cpp",
        "wangle/ssl/SSLSessionCacheManager.cpp",
        "wangle/ssl/SSLUtil.cpp",
        "wangle/ssl/ServerSSLContext.cpp",
        "wangle/ssl/TLSCredProcessor.cpp",
        "wangle/ssl/TLSTicketKeyManager.cpp",
        "wangle/util/FilePoller.cpp",
    ],
    hdrs = glob([
        "wangle/acceptor/**/*.h",
        "wangle/bootstrap/**/*.h",
        "wangle/channel/**/*.h",
        "wangle/client/**/*.h",
        "wangle/codec/**/*.h",
        "wangle/portability/**/*.h",
        "wangle/service/**/*.h",
        "wangle/ssl/**/*.h",
        "wangle/util/**/*.h",
    ]),
    copts = [
        "-std=c++1z",
    ],
    includes = ["."],
    visibility = ["//visibility:public"],
    deps = [
        "@fizz",
        "@folly",
    ],
)
