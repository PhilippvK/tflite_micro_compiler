TF_DIR=../../tensorflow
CXXFLAGS=-g -std=c++11 -DTF_LITE_STATIC_MEMORY -DNDEBUG -O3 -DTF_LITE_DISABLE_X86_NEON \
	-I${TF_DIR} -I${TF_DIR}/tensorflow/lite/micro/tools/make/downloads/ \
	-I${TF_DIR}/tensorflow/lite/micro/tools/make/downloads/gemmlowp \
	-I${TF_DIR}/tensorflow/lite/micro/tools/make/downloads/flatbuffers/include \
	-I${TF_DIR}/tensorflow/lite/micro/tools/make/downloads/ruy \
	-I${TF_DIR}/tensorflow/lite/micro/tools/make/downloads/kissfft
LIBS=-L${TF_DIR}/tensorflow/lite/micro/tools/make/gen/linux_x86_64/lib/ \
	-ltensorflow-microlite

all: hello_world mobilenet hello_world_compiled cifar10 cifar10_compiled

clean:
	$(RM) *.o hello_world mobilnet hello_world_compiled cifar10 cifar10_compiled

mobilenet: mobilenet.o mobilenet_v1_0_25_160_quantized.o gnu.o
	$(CXX) -o $@ $^ ${LIBS}

mobilenet_compiled: mobilenet2.o compiled_mobilenet.o gnu.o
	$(CXX) -o $@ $^ ${LIBS}

hello_world: hello_world.o hello_world_model.o
	$(CXX) -o $@ $^ ${LIBS}

hello_world_compiled: hello_world2.o compiled_hello.o
	$(CXX) -o $@ $^ ${LIBS}

hello_world_model.o: ${TF_DIR}/tensorflow/lite/micro/examples/hello_world/model.cc
	$(CXX) -o $@ -c $^ $(CXXFLAGS)

cifar10: cifar10_model.o truck.o cifar10.o
	$(CXX) -o $@ $^ ${LIBS}

cifar10_compiled: truck.o cifar10_compiled.o cifar10_run_comp.o
	$(CXX) -o $@ $^ ${LIBS}

regenerate: ../compiler
	../compiler hello_world.tflite compiled_hello.cpp hello_ 
	../compiler mobilenet_v1_0_25_160_quantized.tflite compiled_mobilenet.cpp mobilenet_
	../compiler cifar10.tflite cifar10_compiled.cc cifar_
