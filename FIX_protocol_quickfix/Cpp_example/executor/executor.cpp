
#ifdef _MSC_VER
#pragma warning(disable : 4503 4355 4786)
#endif

#include "quickfix/config.h"

#include "quickfix/FileStore.h"
#include "quickfix/SocketAcceptor.h"
#ifdef HAVE_SSL
#include "quickfix/SSLSocketAcceptor.h"
#include "quickfix/ThreadedSSLSocketAcceptor.h"
#endif
#include "Application.h"
#include "quickfix/Log.h"
#include "quickfix/SessionSettings.h"

#include <fstream>
#include <iostream>
#include <memory>
#include <string>

void wait() {
  std::cout << "Type Ctrl-C to quit" << std::endl;
  while (true) {
    FIX::process_sleep(1);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "usage: " << argv[0] << " FILE." << std::endl;
    return 0;
  }
  std::string file = argv[1];
#ifdef HAVE_SSL
  std::string isSSL;
  if (argc > 2) {
    isSSL.assign(argv[2]);
  }
#endif

  try {
    FIX::SessionSettings settings(file);

    Application application;
    FIX::FileStoreFactory storeFactory(settings);
    FIX::ScreenLogFactory logFactory(settings);

    std::unique_ptr<FIX::Acceptor> acceptor;
#ifdef HAVE_SSL
    if (isSSL.compare("SSL") == 0) {
      acceptor = std::unique_ptr<FIX::Acceptor>(
          new FIX::ThreadedSSLSocketAcceptor(application, storeFactory, settings, logFactory));
    } else if (isSSL.compare("SSL-ST") == 0) {
      acceptor
          = std::unique_ptr<FIX::Acceptor>(new FIX::SSLSocketAcceptor(application, storeFactory, settings, logFactory));
    } else
#endif
      acceptor
          = std::unique_ptr<FIX::Acceptor>(new FIX::SocketAcceptor(application, storeFactory, settings, logFactory));

    acceptor->start();
    wait();
    acceptor->stop();

    return 0;
  } catch (std::exception &e) {
    std::cout << e.what() << std::endl;
    return 1;
  }
}
