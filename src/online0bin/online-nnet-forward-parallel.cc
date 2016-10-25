// online0bin/online-nnet-forward-parallel.cc

// Copyright 2015-2016   Shanghai Jiao Tong University (author: Wei Deng)

// See ../../COPYING for clarification regarding multiple authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//  http://www.apache.org/licenses/LICENSE-2.0
//
// THIS CODE IS PROVIDED *AS IS* BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION ANY IMPLIED
// WARRANTIES OR CONDITIONS OF TITLE, FITNESS FOR A PARTICULAR PURPOSE,
// MERCHANTABLITY OR NON-INFRINGEMENT.
// See the Apache 2 License for the specific language governing permissions and
// limitations under the License.

#include <limits>
#include <signal.h>
#include "base/kaldi-common.h"
#include "util/common-utils.h"
#include "base/timer.h"

#include "nnet0/nnet-nnet.h"
#include "online0/online-nnet-forwarding.h"
#include "online0/kaldi-unix-domain-socket-server.h"

int main(int argc, char *argv[]) {
	  using namespace kaldi;
	  using namespace kaldi::nnet0;
	  typedef kaldi::int32 int32;

  try {
    const char *usage =
        "Perform forward pass through Neural Network in online decoding.\n"
        "\n"
        "Usage:  online-nnet-forward-parallel [options] <model-in> <socket-pathname>  \n"
        "e.g.: \n"
        " online-nnet-forward-parallel final.nnet /tmp/forward.socket\n";

    ParseOptions po(usage);

    PdfPriorOptions prior_opts;
    prior_opts.Register(&po);

    OnlineNnetForwardingOptions opts(&prior_opts);
    opts.Register(&po);


    po.Read(argc, argv);

    if (po.NumArgs() != 2) {
      po.PrintUsage();
      exit(1);
    }

    std::string model_filename = po.GetArg(1),
    		socket_filepath = po.GetArg(2);

    //Select the GPU
#if HAVE_CUDA==1
    if (opts.use_gpu == "yes")
        CuDevice::Instantiate().Initialize();
    //CuDevice::Instantiate().DisableCaching();
#endif


    int num_threads = opts.num_threads;
    int num_stream = opts.num_stream;

    signal(SIGPIPE, SIG_IGN);

    std::vector<std::vector<UnixDomainSocket*> > client_list(num_threads);
    std::vector<MultiThreader<OnlineNnetForwardingClass> *> forward_thread(num_threads, NULL);
    UnixDomainSocketServer *server = new UnixDomainSocketServer(socket_filepath);
    UnixDomainSocket *client = NULL;
    ForwardSync forward_sync;

    for (int i = 0; i < num_threads; i++) {
    	client_list[i].resize(num_stream, NULL);

		// initialize forward thread
		// forward_thread[i] = new OnlineNnetForwardingClass(opts, client_list[i], model_filename);
		OnlineNnetForwardingClass *forwarding = new OnlineNnetForwardingClass(opts, client_list[i], forward_sync, model_filename);
		// The initialization of the following class spawns the threads that
		// process the examples.  They get re-joined in its destructor.
		// MultiThreader<OnlineNnetForwardingClass> m(1, *forward_thread[i]);
		forward_thread[i] = new  MultiThreader<OnlineNnetForwardingClass>(1, *forwarding);
    }

    Timer time;
    double time_now = 0;

    KALDI_LOG << "Nnet Forward STARTED";

    // accept client decoder request
    while (true)
    {
    	client = server->Accept(false); // non block

    	if (client == NULL) {
    		const char *c = strerror(errno);
    		if (c == NULL) { c = "[NULL]"; }
    		KALDI_WARN << "Error accept socket, errno was: " << c;
    		continue;
    	}

    	bool success = false;
    	for (int i = 0; i < client_list.size(); i++) {
    		for (int s = 0; s < num_stream; s++) {
    			if (client_list[i][s] == NULL) {
					client_list[i][s] = client;
					success = true;
					KALDI_LOG << "client decoder " << i*num_stream+s << " connected.";
					break;
				}
    		}
            if (success) break;
    	}

    	// create new forward thread for more client decoder
    	if (!success)
    	{
    		std::vector<UnixDomainSocket*> newlist(num_stream, NULL);
    		client_list.push_back(newlist);

            int size = client_list.size();
			client_list[size-1][0] = client;
            forward_thread.resize(size);
    		// initialize forward thread
		    // forward_thread[size-1] = new OnlineNnetForwardingClass(opts, client_list[size-1], model_filename);
		    // MultiThreader<OnlineNnetForwardingClass> m(1, *forward_thread[size-1]);
		    OnlineNnetForwardingClass *forwarding = new OnlineNnetForwardingClass(opts, client_list[size-1], forward_sync, model_filename);
		    forward_thread[size-1] = new  MultiThreader<OnlineNnetForwardingClass>(1, *forwarding);
    	}
    }

    for (int i = 0; i < forward_thread.size(); i++)
        delete forward_thread[i];

    KALDI_LOG << "Nnet Forward FINISHED; ";

    time_now = time.Elapsed();


#if HAVE_CUDA==1
    if (kaldi::g_kaldi_verbose_level >= 1) {
      CuDevice::Instantiate().PrintProfile();
    }
#endif

    return 0;
  } catch(const std::exception &e) {
    std::cerr << e.what();
    return -1;
  }
}