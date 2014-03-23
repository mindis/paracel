Paracel Overview
================

Paracel is a distributed computational framework, designed for many machine learning problems: Logistic Regression, SVD, Matrix Factorization(BFGS, sgd, als, cg), LDA, Lasso...

Firstly, paracel splits both massive dataset and massive parameter space. Unlike Mapreduce-Like Systems, paracel offers a simple communication model, allowing you to work with a global and distributed key-value storage, which is called parameter server.

Upon using paracel, you can build algorithms with following rules: 'pull parameters before learning & push local updates after learning'. Paracel is rather a simple model compared to MPI, and is almost painless transforming from serial to parallel. 

Secondly, paracel try to solve the 'last-reducer' problem of iterative tasks. We use bounded staleness and find a sweet spot between 'improve-iter' curve and 'iter-sec' curve. A global scheduler takes charge of asynchrounous working. This method is already proved to be a generalization of Bsp/Pregel by CMU.

Another advantage of paracel is fault tolerance while MPI has no idea with that.

Paracel can also be used for scientific computing and building graph algorithms. You can load your input in distributed file system and construct a graph, sparse/dense matrix in paracel.

Paracel is originally motivated by Jeff Dean's [talk](http://infolab.stanford.edu/infoseminar/dean.pdf) @Stanford in 2013. You can get more details in his paper: "[Large Scale Distributed Deep Networks](http://static.googleusercontent.com/media/research.google.com/en//archive/large_deep_networks_nips2012.pdf)".

More documents can be found [here](http://xunzhang.github.io/paracel).

Have fun!

License
-------
Paracel is made available under the terms of the BSD License. See the LICENSE file that accompanies this distribution for the full text.