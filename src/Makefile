compile:
	g++ -O2 -fPIC -shared -I $(JAVA_HOME)/include -I $(JAVA_HOME)/include/darwin -olibdumper.dylib dumper.cpp
	java -Xint -XX:+UnlockDiagnosticVMOptions -XX:+PrintInlining -agentpath:libdumper.so=out:output.log,target_method:increment,target_method_signature:\(IZ\)I SimpleClass

compile_java:
	javac -g SimpleClass.java
