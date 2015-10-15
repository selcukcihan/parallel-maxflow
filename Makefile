all: cpp_tbb c_gcc cpp_gcc c_serial cpp_serial Makefile


cpp_tbb: main.c fprf.c fprf_queue.c fprf.h common.h fprf_queue.h fprf_queue_struct.h linux_atomic.h common.h
	g++ -x c++ -O3 -Winline -Wall -DNDEBUG -DTBB_ATOMIC -o fprf_cpp_tbb main.c fprf.c fprf_queue.c -lpthread ${FLAGS}
	g++ -x c++ -pg -Winline -Wall -DDEBUG -DTBB_ATOMIC -DPROFILE_WITH_GPROF -o fprf_cpp_tbb_profile main.c fprf.c fprf_queue.c -lpthread ${FLAGS}
	g++ -x c++ -g -Winline -Wall -DDEBUG -DTBB_ATOMIC -o fprf_cpp_tbb_debug main.c fprf.c fprf_queue.c -lpthread ${FLAGS}

c_gcc: main.c fprf.c fprf_queue.c fprf.h common.h fprf_queue.h fprf_queue_struct.h linux_atomic.h common.h
	gcc -O3 -Winline -Wall -DNDEBUG -o fprf_c_gcc main.c fprf.c fprf_queue.c -lpthread ${FLAGS}
	gcc -pg -Winline -Wall -DDEBUG -DPROFILE_WITH_GPROF -o fprf_c_gcc_profile main.c fprf.c fprf_queue.c -lpthread ${FLAGS}
	gcc -g -Winline -Wall -DDEBUG -o fprf_c_gcc_debug main.c fprf.c fprf_queue.c -lpthread ${FLAGS}

cpp_gcc: main.c fprf.c fprf_queue.c fprf.h common.h fprf_queue.h fprf_queue_struct.h linux_atomic.h common.h
	g++ -x c++ -O3 -Winline -Wall -DNDEBUG -o fprf_cpp_gcc main.c fprf.c fprf_queue.c -lpthread ${FLAGS}
	g++ -x c++ -pg -Winline -Wall -DDEBUG -DPROFILE_WITH_GPROF -o fprf_cpp_gcc_profile main.c fprf.c fprf_queue.c -lpthread ${FLAGS}
	g++ -x c++ -g -Winline -Wall -DDEBUG -o fprf_cpp_gcc_debug main.c fprf.c fprf_queue.c -lpthread ${FLAGS}

c_serial: main.c fprf.c fprf_queue.c fprf.h common.h fprf_queue.h fprf_queue_struct.h linux_atomic.h common.h
	gcc -O3 -Winline -Wall -DNDEBUG -DNO_PARALLEL -o fprf_c_serial main.c fprf.c fprf_queue.c -lpthread ${FLAGS}
	gcc -pg -Winline -Wall -DDEBUG -DNO_PARALLEL -DPROFILE_WITH_GPROF -o fprf_c_serial_profile main.c fprf.c fprf_queue.c -lpthread ${FLAGS}
	gcc -g -Winline -Wall -DDEBUG -DNO_PARALLEL -o fprf_c_serial_debug main.c fprf.c fprf_queue.c -lpthread ${FLAGS}

cpp_serial: main.c fprf.c fprf_queue.c fprf.h common.h fprf_queue.h fprf_queue_struct.h linux_atomic.h common.h
	g++ -x c++ -O3 -Winline -Wall -DNDEBUG -DNO_PARALLEL -o fprf_cpp_serial main.c fprf.c fprf_queue.c -lpthread ${FLAGS}
	g++ -x c++ -pg -Winline -Wall -DDEBUG -DNO_PARALLEL -DPROFILE_WITH_GPROF -o fprf_cpp_serial_profile main.c fprf.c fprf_queue.c -lpthread ${FLAGS}
	g++ -x c++ -g -Winline -Wall -DDEBUG -DNO_PARALLEL -o fprf_cpp_serial_debug main.c fprf.c fprf_queue.c -lpthread ${FLAGS}


clean:
	rm -f fprf_cpp_* fprf_c_*
