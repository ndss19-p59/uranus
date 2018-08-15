# Uranus

Uranus is a easy-to-use SGX runtime based on OpenJDK. It provides
two annotation primitives: JECall and JOCall. For more details
on Uranus's design, please see the paper.

## How to build and run Uranus

Building Uranus is as easy as building OpenJDK. We have tested running hv6 with the following setup:

- Linux Ubuntu 16.04 LTS
- Intel SGX SDK - 1.9

To compile:

    ./confiure; make

To compile and test your build, compile and run the Java programs in ev_test with the built.

## Uranus-based systems
We have built four systems, namely Spark-Uranus, ZooKeeper-Uranus, KV-Uranus and Angle-Uranus (Angle-Uranus is recently supported).

Angel-Uranus is a parameter server based on Angel, developed by Tencent with Java and Scala, that preserves data confidentiality. Angel-Uranus annotates Task.runUser()  with @JECall in Angel to put user-defined data processing tasks into enclaves. Task.runUser() executes user-defined input preprocessing functions and training algorithms in workers (coloured in orange). The annotation prevents cloud providers from accessing the user records. User records outside enclaves are encrypted while parameters are in plaintext. Figure below shows the architecture of Angle-Uranus.

![][1]

[1]: img/angle_uranus.png