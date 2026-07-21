/* 
    Aim is to test various abilities of c++ and ensure that we are able to capture some of the basics we are after such as plotting via matplotlib-cpp and also replicating some of the mech0020 features such as the basic SWE system
*/


// Visualize a graph. Minimal example showing how a default graph appears
#include <sm/vvec> // vvec is part of Seb's maths library
#include <mplot/Visual.h>
#include <mplot/GraphVisual.h>

int main()
{
    // Set up your mplot::Visual 'scene environment'.
    mplot::Visual v(1024, 768, "Made with mplot::GraphVisual");
    // Create a new GraphVisual object with offset within the scene of 0,0,0
    auto gv = std::make_unique<mplot::GraphVisual<double>> (sm::vec<float>({0,0,0}));
    // Boilerplate bindmodel function call - do this for every model you add to a Visual
    v.bindmodel (gv);
    // Data for the x axis. sm::vvec is like std::vector, but with built-in maths methods
    sm::vvec<double> x;
    // This works like numpy's linspace() (the 3 args are "start", "end" and "num"):
    x.linspace (-0.5, 0.8, 14);
    // Set a graph up of y = x^3
    gv->setdata (x, x.pow(3));
    // finalize() makes the GraphVisual compute the vertices of the OpenGL model
    gv->finalize();
    // Add the GraphVisual OpenGL model to the Visual scene (which takes ownership of the unique_ptr)
    v.addVisualModel (gv);
    // Render the scene on the screen until user quits with 'Ctrl-q'
    v.keepOpen();
}