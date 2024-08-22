# Houdini Gaussian Splatting Viewport Renderer
A HDK/GLSL implementation of Gaussian Splatting in Houdini

[![Watch the video](https://i.vimeocdn.com/video/1851733134-68364e97e7700b64d42eb89669d26a56027175614256171f47b983ad7da5fa4f-d?f=webp)](https://vimeo.com/945995885)  
[Watch the video](https://vimeo.com/945995885)

# Context

In November 2023, while sitting in the dentist's chair trying to escape to my happy place, it struck me: despite the surge in Gaussian Splatting rasterisers, none had made their way into Houdini's viewport (at least, none that I knew of). At that moment, it seemed criminal, though maybe that was just the anesthesia wearing off and my irritability setting in.

Jokes aside, on my way home, I decided to give it a go. I had never used Houdini's HDK so I saw this as the greatest excuse I could come up with to finally play with it. I was curious about whether it was possible to implement a viewport rasteriser decently without having to go deeper than what the HDK allows. However, I was convinced that, if possible, Houdini was going to be a great fit. Fast-forward a few weeks, and my conviction had grown to a point where I don't imagine Houdini in the future without the ability to render this type of data natively. 

While there's still a lot to refine, I figured now is a good time to share it with the community. I hope you find it interesting! Don't hesitate to reach out, I'm always happy to chat about cool CG stuff.


# At a high level...

Without getting bogged down in too many details (you'll probably get more out of the code itself), here's a broad overview of what's happening:

1) We define a custom primitive type (the "GSplat"), which is basically a cluster of points with some required attributes (orient, opacity, scale and spherical harmonics coefficients). A SOP node creates these primitives for the incoming points.
2)  We instruct Houdini about how to render these custom primitives. This is, of course, easier said than done as G-splatting via rasterisation requires a very particular setup (there are plenty of resources).
3) The rendering of these primitives needs to be coordinated globally for the viewport. This is achieved via a viewport render hook in this implementation.


# How to install

This implementation requires compiling source code. For convenience I am providing pre-compiled binary files for Windows and MacOS. You can find the files under the `compiled` folder in this repository. It should be just a matter of placing the file that matches your platform (Windows/MacOS/Linux) and Houdini version in the appropriate folder path, as described [here](https://www.sidefx.com/docs/hdk/_h_d_k__intro__creating_plugins.html).

Compiling from source yourself is a bit more involved but not too bad. You can read all about it [here](https://www.sidefx.com/docs/hdk/_h_d_k__intro__compiling.html) and define a workflow that works for you. My approach has been to use Houdini's `hcustom` command. Below the simplest snippet I could come up with that gets the job done, hopefully useful to get you started setting up a workflow that suits your needs.

```
export HOUDINI_VERSION="20.5.278"
cd "/opt/hfs${HOUDINI_VERSION}"
source houdini_setup
cd <PATH_TO_REPOSITORY_BASE>/gsplat_plugin
hcustom -I include -I shaders gsplat_plugin.C
```

# How to use

Once the plugin is picked up by Houdini, you should be able use it. In this repository I provide an example hipfile `hip/GSplatPlugin_simpleScene_v001.hipnc` that you can check out to get the idea. I also suggest you setup your viewport in a certain way as shown in the video below:

Click on the image below to see my suggested steps:
[![Watch the video](https://i.vimeocdn.com/video/1917607128-7eb702c79bfda91c3f2cc8efe005038f15c23b8fee5802ee197f949a5256d280-d?f=webp)](https://vimeo.com/1001396463)  
[Watch the video](https://vimeo.com/1001396463)

Please, don't think about the `GSplatSOP` as "the renderer". All that the SOP does is to create the custom primitive types from the incoming points based on their attributes. From that point on, provided the GSplat primitives exist in a Houdini Geometry that is being displayed (whether they come from one or multiple GSplatSOPs), a global "renderer" takes care of them.

# What's left to do...

Plenty!

I've always taken this project as a playground, and I intend to carry on exploring new ideas and refining things in the near future, there are a number of things I have started playing with and might drop in the near future. Some of these things relate to solving known issues, other are improvements and new features. Below a non-comprehensive list of things on my radar:

Limitations:
- Expect this to work only on a single viewport
- No Spherical Harmonics rotation currently
- No integration of other 3D elements with GSplats
- Amount of GSplats (~2M on my Mabook Pro M1)

Improvements/New Features:
- Performance improvements for time-dependent data
- Fast time-dependent GSplats
- Debug and other visualisation modes
- GSplats as IBLs

Having said that, please note that I can't currently commit to a roadmap or anything of the like. 

# Acknowledgements
I don't think I would have gotten to where I got to without so many open-source projects to draw inspiration from. I would like to highlight these two, since although I have not directly reached out to the authors, they have been massively helpful so I wanted to give special kudos to them.

https://github.com/aras-p/UnityGaussianSplatting
https://github.com/antimatter15/splat


If you make something cool and share it on social media, I'd love to check it out, please consider tagging [me](https://www.linkedin.com/in/rubendz/) :)

Keep splatting!

