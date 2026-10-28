#pragma once
// Verified TLS for bolt+s://. Included by driver.hpp only when the build
// defines GALACTUS_WITH_TLS (CMake option GALACTUS_WITH_TLS).
//
// Backends: SChannel on Windows (link secur32 and crypt32), OpenSSL on Linux
// and macOS (link ssl and crypto). Defining GALACTUS_TLS_OPENSSL selects
// OpenSSL on Windows as well.
//
// Both backends send the URI host as SNI and check the certificate against
// it. Trust comes from the platform store unless a trusted certificate file
// is given; then only the PEM certificates in that file are trusted. There is
// no way to skip verification.
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32) && !defined(GALACTUS_TLS_OPENSSL)
#define GALACTUS_TLS_SCHANNEL 1
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <wincrypt.h>
#include <schannel.h>
#include <security.h>
#include <sspi.h>
#ifdef _MSC_VER
#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "crypt32.lib")
#endif
#else
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

namespace galactus {
namespace detail {

inline std::string hex_status(long status) {
  std::ostringstream s;
  s << "0x" << std::hex << static_cast<unsigned long>(status);
  return s.str();
}

inline bool is_ip_literal(const std::string &host) {
  if (host.find(':') != std::string::npos)
    return true; // IPv6; host names never contain ':'
  return !host.empty() &&
         std::all_of(host.begin(), host.end(),
                     [](char c) { return (c >= '0' && c <= '9') || c == '.'; });
}

#ifdef GALACTUS_TLS_SCHANNEL

// CERT_CHAIN_ENGINE_CONFIG as laid out from Windows 8 on. Older SDK headers
// (and MinGW with a low _WIN32_WINNT) omit the exclusive-root fields, so the
// driver declares the full layout itself; Windows reads cbSize to see them.
struct ChainEngineConfig {
  DWORD cbSize;
  HCERTSTORE hRestrictedRoot;
  HCERTSTORE hRestrictedTrust;
  HCERTSTORE hRestrictedOther;
  DWORD cAdditionalStore;
  HCERTSTORE *rghAdditionalStore;
  DWORD dwFlags;
  DWORD dwUrlRetrievalTimeout;
  DWORD MaximumCachedCertificates;
  DWORD CycleDetectionModulus;
  HCERTSTORE hExclusiveRoot;
  HCERTSTORE hExclusiveTrustedPeople;
  DWORD dwExclusiveFlags;
};

class TlsStream {
  static constexpr DWORD request_flags =
      ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT |
      ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
      ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM |
      ISC_REQ_MANUAL_CRED_VALIDATION;
  SOCKET socket_;
  std::vector<char> target_; // NUL-terminated host for SNI
  CredHandle credentials_{};
  CtxtHandle context_{};
  bool have_credentials_ = false;
  bool have_context_ = false;
  bool failed_ = false;
  SecPkgContext_StreamSizes sizes_{};
  std::vector<std::uint8_t> incoming_; // received, not yet decrypted
  std::vector<std::uint8_t> plain_;    // decrypted, not yet returned
  std::size_t plain_pos_ = 0;

  void send_raw(const std::uint8_t *data, std::size_t n) {
    while (n) {
      int sent = ::send(socket_, reinterpret_cast<const char *>(data),
                        static_cast<int>(std::min<std::size_t>(n, 1 << 30)), 0);
      if (sent <= 0) {
        failed_ = true;
        throw ConnectionError("Socket write failed");
      }
      data += sent;
      n -= static_cast<std::size_t>(sent);
    }
  }
  void receive_more() {
    std::uint8_t buffer[16384];
    int got = ::recv(socket_, reinterpret_cast<char *>(buffer),
                     static_cast<int>(sizeof(buffer)), 0);
    if (got <= 0) {
      failed_ = true;
      throw ConnectionError("Socket read failed or connection closed");
    }
    incoming_.insert(incoming_.end(), buffer, buffer + got);
  }
  void send_token(SecBuffer &token) {
    if (token.pvBuffer && token.cbBuffer)
      send_raw(static_cast<const std::uint8_t *>(token.pvBuffer),
               token.cbBuffer);
  }
  static std::vector<char> nul_terminated(const std::string &s) {
    std::vector<char> v(s.begin(), s.end());
    v.push_back('\0');
    return v;
  }

  void acquire_credentials() {
    SCHANNEL_CRED cred{};
    cred.dwVersion = SCHANNEL_CRED_VERSION;
#ifndef SCH_USE_STRONG_CRYPTO
#define SCH_USE_STRONG_CRYPTO 0x00400000
#endif
    // The driver validates the peer itself (verify_peer) so the same
    // checks run with the platform store and with a trusted certificate file.
    cred.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION |
                   SCH_USE_STRONG_CRYPTO;
    TimeStamp expiry;
    char package[] = UNISP_NAME_A;
    SECURITY_STATUS status = AcquireCredentialsHandleA(
        nullptr, package, SECPKG_CRED_OUTBOUND, nullptr, &cred, nullptr,
        nullptr, &credentials_, &expiry);
    if (status != SEC_E_OK)
      throw TlsError("TLS credentials could not be created (" +
                     hex_status(status) + ")");
    have_credentials_ = true;
  }

  // Runs the client handshake, or continues it after SEC_I_RENEGOTIATE using
  // whatever is already buffered in incoming_.
  void handshake(bool initial) {
    SECURITY_STATUS status;
    DWORD out_flags = 0;
    TimeStamp expiry;
    if (initial) {
      SecBuffer out{0, SECBUFFER_TOKEN, nullptr};
      SecBufferDesc out_desc{SECBUFFER_VERSION, 1, &out};
      status = InitializeSecurityContextA(
          &credentials_, nullptr, target_.data(), request_flags, 0, 0, nullptr,
          0, &context_, &out_desc, &out_flags, &expiry);
      if (status != SEC_I_CONTINUE_NEEDED) {
        if (out.pvBuffer)
          FreeContextBuffer(out.pvBuffer);
        throw TlsError("TLS handshake could not start (" + hex_status(status) +
                       ")");
      }
      have_context_ = true;
      try {
        send_token(out);
      } catch (...) {
        FreeContextBuffer(out.pvBuffer);
        throw;
      }
      FreeContextBuffer(out.pvBuffer);
    }
    status = SEC_I_CONTINUE_NEEDED;
    for (;;) {
      if (incoming_.empty() || status == SEC_E_INCOMPLETE_MESSAGE)
        receive_more();
      SecBuffer in[2] = {{static_cast<unsigned long>(incoming_.size()),
                          SECBUFFER_TOKEN, incoming_.data()},
                         {0, SECBUFFER_EMPTY, nullptr}};
      SecBufferDesc in_desc{SECBUFFER_VERSION, 2, in};
      SecBuffer out{0, SECBUFFER_TOKEN, nullptr};
      SecBufferDesc out_desc{SECBUFFER_VERSION, 1, &out};
      status = InitializeSecurityContextA(
          &credentials_, &context_, target_.data(), request_flags, 0, 0,
          &in_desc, 0, nullptr, &out_desc, &out_flags, &expiry);
      if (status == SEC_E_INCOMPLETE_MESSAGE) {
        if (out.pvBuffer)
          FreeContextBuffer(out.pvBuffer);
        continue; // keep the partial record and read more
      }
      bool send_failed = false;
      if (status == SEC_E_OK || status == SEC_I_CONTINUE_NEEDED ||
          (FAILED(status) && (out_flags & ISC_RET_EXTENDED_ERROR))) {
        try {
          send_token(out); // next handshake flight, or an alert on failure
        } catch (...) {
          send_failed = true;
        }
      }
      if (out.pvBuffer)
        FreeContextBuffer(out.pvBuffer);
      if (FAILED(status)) {
        failed_ = true;
        throw TlsError("TLS handshake failed (" + hex_status(status) + ")");
      }
      if (send_failed)
        throw ConnectionError("Socket write failed during the TLS handshake");
      if (status == SEC_I_INCOMPLETE_CREDENTIALS) {
        // The server asked for a client certificate; continue without one.
        status = SEC_I_CONTINUE_NEEDED;
        continue;
      }
      if (in[1].BufferType == SECBUFFER_EXTRA && in[1].cbBuffer > 0) {
        // Unprocessed bytes sit at the end of the input buffer.
        std::size_t extra = in[1].cbBuffer;
        std::memmove(incoming_.data(),
                     incoming_.data() + incoming_.size() - extra, extra);
        incoming_.resize(extra);
      } else
        incoming_.clear();
      if (status == SEC_E_OK)
        return; // any remaining bytes are application records
      if (status != SEC_I_CONTINUE_NEEDED) {
        failed_ = true;
        throw TlsError("Unexpected TLS handshake status (" +
                       hex_status(status) + ")");
      }
    }
  }

  static HCERTSTORE load_pem_store(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
      throw ConfigurationError("Cannot read trusted certificate file " + path);
    std::string pem((std::istreambuf_iterator<char>(file)),
                    std::istreambuf_iterator<char>());
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0,
                                     CERT_STORE_CREATE_NEW_FLAG, nullptr);
    if (!store)
      throw TlsError("Cannot create a certificate store");
    const std::string begin = "-----BEGIN CERTIFICATE-----";
    const std::string end = "-----END CERTIFICATE-----";
    std::size_t pos = 0;
    int count = 0;
    try {
      for (;;) {
        auto b = pem.find(begin, pos);
        if (b == std::string::npos)
          break;
        auto e = pem.find(end, b);
        if (e == std::string::npos)
          throw ConfigurationError("Unterminated PEM certificate in " + path);
        e += end.size();
        std::string block = pem.substr(b, e - b);
        DWORD length = 0;
        if (!CryptStringToBinaryA(block.c_str(),
                                  static_cast<DWORD>(block.size()),
                                  CRYPT_STRING_BASE64HEADER, nullptr, &length,
                                  nullptr, nullptr))
          throw ConfigurationError("Invalid PEM certificate in " + path);
        std::vector<BYTE> der(length);
        if (!CryptStringToBinaryA(block.c_str(),
                                  static_cast<DWORD>(block.size()),
                                  CRYPT_STRING_BASE64HEADER, der.data(),
                                  &length, nullptr, nullptr) ||
            !CertAddEncodedCertificateToStore(
                store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der.data(),
                length, CERT_STORE_ADD_ALWAYS, nullptr))
          throw ConfigurationError("Invalid PEM certificate in " + path);
        ++count;
        pos = e;
      }
      if (!count)
        throw ConfigurationError("No PEM certificates found in " + path);
    } catch (...) {
      CertCloseStore(store, 0);
      throw;
    }
    return store;
  }

  static std::string chain_error(DWORD error) {
    switch (error) {
    case static_cast<DWORD>(CERT_E_UNTRUSTEDROOT):
      return "the certificate chain ends in an untrusted root";
    case static_cast<DWORD>(CERT_E_CHAINING):
      return "the certificate issuer could not be found";
    case static_cast<DWORD>(CERT_E_CN_NO_MATCH):
      return "the certificate does not match the host name";
    case static_cast<DWORD>(CERT_E_EXPIRED):
      return "the certificate has expired or is not yet valid";
    case static_cast<DWORD>(CERT_E_WRONG_USAGE):
      return "the certificate is not valid for server authentication";
    case static_cast<DWORD>(CERT_E_REVOKED):
      return "the certificate has been revoked";
    default:
      return "certificate error " + hex_status(static_cast<long>(error));
    }
  }

  // Builds the peer chain against the platform store, or only against the
  // trusted store when one is given, then applies the SSL policy with the
  // URI host as the server name.
  void verify_peer(HCERTSTORE trusted, const std::string &host) {
    PCCERT_CONTEXT peer = nullptr;
    if (QueryContextAttributesA(&context_, SECPKG_ATTR_REMOTE_CERT_CONTEXT,
                                &peer) != SEC_E_OK ||
        !peer)
      throw TlsError("TLS certificate verification failed: the server sent "
                     "no certificate");
    HCERTCHAINENGINE engine = nullptr; // default: current user's trust
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    std::string failure;
    if (trusted) {
      ChainEngineConfig config{};
      config.cbSize = sizeof(config);
      config.dwFlags = CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL;
      config.hExclusiveRoot = trusted;
      config.dwExclusiveFlags = 0x1; // CERT_CHAIN_EXCLUSIVE_ENABLE_CA_FLAG
      if (!CertCreateCertificateChainEngine(
              reinterpret_cast<PCERT_CHAIN_ENGINE_CONFIG>(&config), &engine))
        failure = "the trusted certificate store could not be used";
    }
    if (failure.empty()) {
      char server_auth[] = szOID_PKIX_KP_SERVER_AUTH;
      LPSTR usages[] = {server_auth};
      CERT_CHAIN_PARA para{};
      para.cbSize = sizeof(para);
      para.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
      para.RequestedUsage.Usage.cUsageIdentifier = 1;
      para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
      if (!CertGetCertificateChain(engine, peer, nullptr, peer->hCertStore,
                                   &para,
                                   trusted ? CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL
                                           : 0,
                                   nullptr, &chain))
        failure = "the certificate chain could not be built";
    }
    if (failure.empty()) {
      int wide_length =
          MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
      std::vector<wchar_t> wide(wide_length > 0 ? wide_length : 1, L'\0');
      if (wide_length > 0)
        MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, wide.data(),
                            wide_length);
      SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
      ssl.cbSize = sizeof(ssl);
      ssl.dwAuthType = AUTHTYPE_SERVER;
      ssl.fdwChecks = 0;
      ssl.pwszServerName = wide.data();
      CERT_CHAIN_POLICY_PARA policy{};
      policy.cbSize = sizeof(policy);
      policy.pvExtraPolicyPara = &ssl;
      CERT_CHAIN_POLICY_STATUS result{};
      result.cbSize = sizeof(result);
      if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain,
                                            &policy, &result))
        failure = "the certificate policy check could not run";
      else if (result.dwError)
        failure = chain_error(result.dwError);
    }
    if (chain)
      CertFreeCertificateChain(chain);
    if (engine)
      CertFreeCertificateChainEngine(engine);
    CertFreeCertificateContext(peer);
    if (!failure.empty()) {
      failed_ = true;
      throw TlsError("TLS certificate verification failed: " + failure);
    }
  }

  void release() noexcept {
    if (have_context_)
      DeleteSecurityContext(&context_);
    if (have_credentials_)
      FreeCredentialsHandle(&credentials_);
    have_context_ = have_credentials_ = false;
  }

public:
  TlsStream(SOCKET socket, const std::string &host,
            const std::string &trusted_certificates)
      : socket_(socket), target_(nul_terminated(host)) {
    HCERTSTORE trusted = nullptr;
    try {
      if (!trusted_certificates.empty())
        trusted = load_pem_store(trusted_certificates);
      acquire_credentials();
      handshake(true);
      verify_peer(trusted, host);
      SECURITY_STATUS status = QueryContextAttributesA(
          &context_, SECPKG_ATTR_STREAM_SIZES, &sizes_);
      if (status != SEC_E_OK)
        throw TlsError("TLS stream sizes unavailable (" + hex_status(status) +
                       ")");
    } catch (...) {
      if (trusted)
        CertCloseStore(trusted, 0);
      release();
      throw;
    }
    if (trusted)
      CertCloseStore(trusted, 0);
  }
  TlsStream(const TlsStream &) = delete;
  TlsStream &operator=(const TlsStream &) = delete;
  ~TlsStream() { release(); }

  void write(const std::uint8_t *data, std::size_t n) {
    const std::size_t max = sizes_.cbMaximumMessage;
    std::vector<std::uint8_t> record(sizes_.cbHeader + max + sizes_.cbTrailer);
    while (n) {
      std::size_t chunk = std::min(n, max);
      std::memcpy(record.data() + sizes_.cbHeader, data, chunk);
      SecBuffer buffers[4] = {
          {sizes_.cbHeader, SECBUFFER_STREAM_HEADER, record.data()},
          {static_cast<unsigned long>(chunk), SECBUFFER_DATA,
           record.data() + sizes_.cbHeader},
          {sizes_.cbTrailer, SECBUFFER_STREAM_TRAILER,
           record.data() + sizes_.cbHeader + chunk},
          {0, SECBUFFER_EMPTY, nullptr}};
      SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};
      SECURITY_STATUS status = EncryptMessage(&context_, 0, &desc, 0);
      if (status != SEC_E_OK) {
        failed_ = true;
        throw TlsError("TLS encryption failed (" + hex_status(status) + ")");
      }
      send_raw(record.data(), buffers[0].cbBuffer + buffers[1].cbBuffer +
                                  buffers[2].cbBuffer);
      data += chunk;
      n -= chunk;
    }
  }

  std::size_t read(std::uint8_t *out, std::size_t n) {
    bool need_more = false;
    while (plain_pos_ == plain_.size()) {
      plain_.clear();
      plain_pos_ = 0;
      if (incoming_.empty() || need_more)
        receive_more();
      SecBuffer buffers[4] = {{static_cast<unsigned long>(incoming_.size()),
                               SECBUFFER_DATA, incoming_.data()},
                              {0, SECBUFFER_EMPTY, nullptr},
                              {0, SECBUFFER_EMPTY, nullptr},
                              {0, SECBUFFER_EMPTY, nullptr}};
      SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};
      SECURITY_STATUS status = DecryptMessage(&context_, &desc, 0, nullptr);
      if (status == SEC_E_INCOMPLETE_MESSAGE) {
        need_more = true;
        continue;
      }
      need_more = false;
      if (status == SEC_I_CONTEXT_EXPIRED) {
        failed_ = true; // close_notify received
        throw ConnectionError("TLS connection closed by the server");
      }
      if (status != SEC_E_OK && status != SEC_I_RENEGOTIATE) {
        failed_ = true;
        throw TlsError("TLS decryption failed (" + hex_status(status) + ")");
      }
      SecBuffer *data = nullptr, *extra = nullptr;
      for (int i = 1; i < 4; i++) {
        if (buffers[i].BufferType == SECBUFFER_DATA && !data)
          data = &buffers[i];
        else if (buffers[i].BufferType == SECBUFFER_EXTRA && !extra)
          extra = &buffers[i];
      }
      // Copy the plaintext out before compacting: both point into incoming_.
      if (data && data->cbBuffer) {
        auto p = static_cast<const std::uint8_t *>(data->pvBuffer);
        plain_.assign(p, p + data->cbBuffer);
      }
      if (extra && extra->cbBuffer) {
        std::size_t count = extra->cbBuffer;
        std::memmove(incoming_.data(), extra->pvBuffer, count);
        incoming_.resize(count);
      } else
        incoming_.clear();
      if (status == SEC_I_RENEGOTIATE)
        handshake(false);
    }
    std::size_t count = std::min(n, plain_.size() - plain_pos_);
    std::memcpy(out, plain_.data() + plain_pos_, count);
    plain_pos_ += count;
    return count;
  }

  // Sends close_notify when the session is still healthy. Never throws.
  void shutdown() noexcept {
    if (!have_context_ || failed_)
      return;
    DWORD type = SCHANNEL_SHUTDOWN;
    SecBuffer control{sizeof(type), SECBUFFER_TOKEN, &type};
    SecBufferDesc control_desc{SECBUFFER_VERSION, 1, &control};
    if (ApplyControlToken(&context_, &control_desc) != SEC_E_OK)
      return;
    SecBuffer out{0, SECBUFFER_TOKEN, nullptr};
    SecBufferDesc out_desc{SECBUFFER_VERSION, 1, &out};
    DWORD out_flags = 0;
    TimeStamp expiry;
    SECURITY_STATUS status = InitializeSecurityContextA(
        &credentials_, &context_, target_.data(), request_flags, 0, 0, nullptr,
        0, nullptr, &out_desc, &out_flags, &expiry);
    if (!FAILED(status) && out.pvBuffer && out.cbBuffer)
      ::send(socket_, static_cast<const char *>(out.pvBuffer),
             static_cast<int>(out.cbBuffer), 0);
    if (out.pvBuffer)
      FreeContextBuffer(out.pvBuffer);
  }
};

#else // OpenSSL

class TlsStream {
  SSL_CTX *context_ = nullptr;
  SSL *ssl_ = nullptr;
  bool failed_ = false;

  static std::string openssl_errors() {
    std::string text;
    while (unsigned long e = ERR_get_error()) {
      char buffer[256];
      ERR_error_string_n(e, buffer, sizeof(buffer));
      if (!text.empty())
        text += "; ";
      text += buffer;
    }
    return text.empty() ? "unknown error" : text;
  }
  void release() noexcept {
    if (ssl_)
      SSL_free(ssl_);
    if (context_)
      SSL_CTX_free(context_);
    ssl_ = nullptr;
    context_ = nullptr;
  }

public:
  template <class Socket>
  TlsStream(Socket socket, const std::string &host,
            const std::string &trusted_certificates) {
    OPENSSL_init_ssl(0, nullptr);
    ERR_clear_error();
    try {
      context_ = SSL_CTX_new(TLS_client_method());
      if (!context_)
        throw TlsError("TLS context could not be created: " +
                       openssl_errors());
      SSL_CTX_set_min_proto_version(context_, TLS1_2_VERSION);
      SSL_CTX_set_verify(context_, SSL_VERIFY_PEER, nullptr);
      if (!trusted_certificates.empty()) {
        // Only the certificates in this file are trusted.
        if (SSL_CTX_load_verify_locations(
                context_, trusted_certificates.c_str(), nullptr) != 1)
          throw ConfigurationError("Cannot load trusted certificate file " +
                                   trusted_certificates + ": " +
                                   openssl_errors());
      } else if (SSL_CTX_set_default_verify_paths(context_) != 1)
        throw TlsError("The platform certificate store could not be loaded: " +
                       openssl_errors());
      ssl_ = SSL_new(context_);
      if (!ssl_ || SSL_set_fd(ssl_, static_cast<int>(socket)) != 1)
        throw TlsError("TLS session could not be created: " +
                       openssl_errors());
      if (is_ip_literal(host)) {
        if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl_),
                                          host.c_str()) != 1)
          throw TlsError("Invalid IP address for TLS verification");
      } else {
        if (SSL_set_tlsext_host_name(ssl_, host.c_str()) != 1)
          throw TlsError("TLS server name could not be set: " +
                         openssl_errors());
        SSL_set_hostflags(ssl_, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (SSL_set1_host(ssl_, host.c_str()) != 1)
          throw TlsError("TLS host name check could not be set: " +
                         openssl_errors());
      }
      if (SSL_connect(ssl_) != 1) {
        failed_ = true;
        long verify = SSL_get_verify_result(ssl_);
        if (verify != X509_V_OK)
          throw TlsError(std::string("TLS certificate verification failed: ") +
                         X509_verify_cert_error_string(verify));
        throw TlsError("TLS handshake failed: " + openssl_errors());
      }
    } catch (...) {
      release();
      throw;
    }
  }
  TlsStream(const TlsStream &) = delete;
  TlsStream &operator=(const TlsStream &) = delete;
  ~TlsStream() { release(); }

  void write(const std::uint8_t *data, std::size_t n) {
    while (n) {
      int chunk = static_cast<int>(std::min<std::size_t>(n, 1 << 30));
      int written = SSL_write(ssl_, data, chunk);
      if (written <= 0) {
        failed_ = true;
        throw ConnectionError("TLS write failed: " + openssl_errors());
      }
      data += written;
      n -= static_cast<std::size_t>(written);
    }
  }
  std::size_t read(std::uint8_t *out, std::size_t n) {
    int got =
        SSL_read(ssl_, out, static_cast<int>(std::min<std::size_t>(n, 1 << 30)));
    if (got > 0)
      return static_cast<std::size_t>(got);
    failed_ = true;
    if (SSL_get_error(ssl_, got) == SSL_ERROR_ZERO_RETURN)
      throw ConnectionError("TLS connection closed by the server");
    throw ConnectionError("TLS read failed or connection closed: " +
                          openssl_errors());
  }
  // Sends close_notify when the session is still healthy. Never throws.
  void shutdown() noexcept {
    if (ssl_ && !failed_)
      SSL_shutdown(ssl_);
  }
};

#endif
} // namespace detail
} // namespace galactus
