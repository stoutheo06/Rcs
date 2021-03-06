/*******************************************************************************

  Copyright (c) 2017, Honda Research Institute Europe GmbH.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  3. All advertising materials mentioning features or use of this software
     must display the following acknowledgement: This product includes
     software developed by the Honda Research Institute Europe GmbH.

  4. Neither the name of the copyright holder nor the names of its
     contributors may be used to endorse or promote products derived from
     this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER "AS IS" AND ANY EXPRESS OR
  IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
  OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
  IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
  OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
  EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.





  This class is based on the OpenSceneGraph example osghud, which is licensed as

  OpenSceneGraph example, osghud.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.

*******************************************************************************/


/*******************************************************************************

  The dynamic text update has problems when called from different threads. The
  problems result in segmentation faults. To run it safely, the viewer's
  threading model should be changed to CullDrawThreadPerContext (or
  SingleThreaded which is slower). That fixes the segmentation faults.

*******************************************************************************/

#include "HUD.h"
#include "Rcs_graphicsUtils.h"
#include "Rcs_resourcePath.h"
#include "Rcs_macros.h"

#include <osg/MatrixTransform>

#define HUD_FONTSIZE   (25)



/*******************************************************************************
 * Update the text
 ******************************************************************************/
namespace Rcs
{

class HUDUpdateCallback : public osg::Drawable::UpdateCallback
{
public:

  HUDUpdateCallback(HUD* hud_, size_t margin_=10) :
    hud(hud_), margin(margin_)
  {
  }

  virtual void update(osg::NodeVisitor* nv, osg::Drawable* node)
  {
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(hud->_mutex);

    if (hud->textChanged==true)
    {
      hud->hudText->setText(hud->_text);
      hud->textChanged = false;

      // If the text has changed, we auto-scale the background in case the
      // autoScale option is selected.
      if (hud->autoScale==true)
      {
        hud->backgroundChanged = true;
      }

    }   // if (hud->textChanged==true)

    if (hud->backgroundChanged==true)
    {
      if (hud->autoScale==true)
      {
        osg::BoundingBox bb = hud->hudText->getBound();
        int newSizeX = (int)(bb.xMax()-bb.xMin())+2*margin;
        int newSizeY = (int)(bb.yMax()-bb.yMin())+2*margin;

        // In auto scale mode, only grow the HUD
        if (newSizeX<hud->sizeX)
        {
          newSizeX = hud->sizeX;
        }

        if (newSizeY<hud->sizeY)
        {
          newSizeY = hud->sizeY;
        }

        hud->resizeNoMutex(hud->llx, hud->lly, newSizeX, newSizeY);
      }

      hud->backgroundChanged = false;
      hud->bgGeometry->setVertexArray(hud->backgroundVertices.get());
      hud->hudText->setPosition(osg::Vec3(hud->llx+margin,
                                          hud->lly+hud->sizeY-margin, -1.0));
    }   // if (hud->backgroundChanged==true)


  }

  HUD* hud;
  size_t margin;
};

}   // namespace Rcs

/*******************************************************************************
 *
 ******************************************************************************/
Rcs::HUD::HUD() :
  osg::Camera(), textChanged(true), backgroundChanged(true), autoScale(true),
  llx(0), lly(0), sizeX(0), sizeY(0)
{
  init(llx, lly, sizeX, sizeY, "WHITE");
}

/*******************************************************************************
 *
 ******************************************************************************/
Rcs::HUD::HUD(const char* textColor) :
  osg::Camera(), textChanged(true), backgroundChanged(true), autoScale(true),
  llx(0), lly(0), sizeX(0), sizeY(0)
{
  init(llx, lly, sizeX, sizeY, textColor);
}

/*******************************************************************************
 *
 ******************************************************************************/
Rcs::HUD::HUD(int llx_, int lly_, int sizeX_, int sizeY_,
              const char* textColor) :
  osg::Camera(), textChanged(true), backgroundChanged(true), autoScale(false),
  llx(llx_), lly(lly_), sizeX(sizeX_), sizeY(sizeY_)
{
  init(llx, lly, sizeX, sizeY, textColor);
}

/*******************************************************************************
 *
 ******************************************************************************/
void Rcs::HUD::init(int llx, int lly, int sizeX, int sizeY,
                    const char* textColor)
{
  const size_t margin = 10;

  // Set up background rectangle
  this->backgroundVertices = new osg::Vec3Array;
  osg::ref_ptr<osg::Vec3Array> normals = new osg::Vec3Array;
  normals->push_back(osg::Vec3(0.0f, 0.0f, 1.0f));
  osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
  colors->push_back(osg::Vec4(0.8f, 0.8f, 0.8f, 0.8f));
  backgroundVertices->push_back(osg::Vec3(llx,          lly,         -1.0));
  backgroundVertices->push_back(osg::Vec3(llx + sizeX,  lly,         -1.0));
  backgroundVertices->push_back(osg::Vec3(llx + sizeX,  lly + sizeY, -1.0));
  backgroundVertices->push_back(osg::Vec3(llx,          lly + sizeY, -1.0));

  // Set up background geometry
  this->bgGeometry = new osg::Geometry;
  bgGeometry->setVertexArray(backgroundVertices.get());
  bgGeometry->setNormalArray(normals.get());
  bgGeometry->setColorArray(colors.get());
  bgGeometry->addPrimitiveSet(new osg::DrawArrays(GL_QUADS,0,4));

  // Create text instance that will show up in the HUD
  this->hudText = new osgText::Text();
  hudText->setAxisAlignment(osgText::Text::SCREEN);
  hudText->setAlignment(osgText::TextBase::LEFT_TOP);
  hudText->setColor(colorFromString(textColor));

  char fontFile[256];
  bool fontFound = Rcs_getAbsoluteFileName("fonts/VeraMono.ttf", fontFile);

  if (fontFound == true)
  {
    hudText->setCharacterSize(HUD_FONTSIZE);
    hudText->setFont(fontFile);
  }
  else
  {
    RLOG(4, "Couldn't find font file in resource path");
    hudText->setCharacterSize(50);
  }

  // Add to this camera a subgraph to render
  this->geode = new osg::Geode();

  // Turn off lighting for the text
  // Disable depth testing to ensure it's always ontop
  // Turn on blending so that alpha of texture is correct
  // Make sure this geometry is draw last.
  osg::StateSet* hudState = geode->getOrCreateStateSet();
  hudState->setMode(GL_LIGHTING,osg::StateAttribute::OFF);
  hudState->setMode(GL_DEPTH_TEST, osg::StateAttribute::OFF);
  hudState->setMode(GL_BLEND, osg::StateAttribute::ON);
  hudState->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
  hudState->setRenderBinDetails(11, "RenderBin");

  // Add the text (Text class is derived from drawable) to the geode:
  geode->addDrawable(hudText.get());
  geode->addDrawable(bgGeometry.get());


  // For the HUD model view matrix use an identity matrix:
  osg::ref_ptr<osg::MatrixTransform> mdlViewMat = new osg::MatrixTransform;
  mdlViewMat->setMatrix(osg::Matrix::identity());
  mdlViewMat->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
  mdlViewMat->addChild(geode.get());


  // Set the projection matrix
  osg::ref_ptr<osg::Projection> prjMat = new osg::Projection;
  prjMat->setMatrix(osg::Matrix::ortho2D(0, 1024, 0, 768));
  prjMat->addChild(mdlViewMat.get());

  addChild(prjMat.get());

  // Only clear the depth buffer
  setClearMask(GL_DEPTH_BUFFER_BIT);

  // Draw subgraph after main camera view.
  setRenderOrder(osg::Camera::POST_RENDER);

  // Disallow camera to grab event focus from the viewers main camera(s).
  setAllowEventFocus(false);

  // Thread-safe update callback for applying text changes
  hudText->setUpdateCallback(new HUDUpdateCallback(this, margin));
  setName("Rcs::HUD");
}

/*******************************************************************************
 *
 ******************************************************************************/
Rcs::HUD::~HUD()
{
}

/*******************************************************************************
 * Sets the text. We copy it into an internal buffer. The update is
 * done by an update callback that is called upon the render
 * traversal. Otherwise we get problems with different threads if the
 * setText() calls interfere with the update callback.
 *
 * We use a mutex to avoid undefined behaviour from multiple threads
 * writing to the HUD.
 ******************************************************************************/
void Rcs::HUD::setText(const std::string& text)
{
  if (_text != text)
  {
    OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
    textChanged = true;
    _text = text;
  }

}

/*******************************************************************************
 * See above
 ******************************************************************************/
void Rcs::HUD::setText(const char* text)
{
  if (text==NULL)
  {
    RLOG(1, "Text is NULL - ignoring");
    return;
  }

  setText(std::string(text));
}

/*******************************************************************************
 * See above
 ******************************************************************************/
void Rcs::HUD::setText(const std::stringstream& text)
{
  setText(text.str());
}

/*******************************************************************************
 *
 ******************************************************************************/
const char* Rcs::HUD::className() const
{
  return "Rcs::HUD";
}

/*******************************************************************************
 *
 ******************************************************************************/
void Rcs::HUD::setFontSize(int fontSize)
{
  OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
  hudText->setCharacterSize(fontSize);
}

/*******************************************************************************
 *
 ******************************************************************************/
void Rcs::HUD::setColor(const char* colorName)
{
  osg::Vec4 colorVec = colorFromString(colorName);
  OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
  hudText->setColor(colorVec);
}

/*******************************************************************************
 *
 ******************************************************************************/
void Rcs::HUD::resize(int sizeX_, int sizeY_)
{
  resize(llx, lly, sizeX, sizeY);
}

/*******************************************************************************
 *
 ******************************************************************************/
void Rcs::HUD::resize(int llx_, int lly_, int sizeX_, int sizeY_)
{
  OpenThreads::ScopedLock<OpenThreads::Mutex> lock(_mutex);
  resizeNoMutex(llx_, lly_, sizeX_, sizeY_);
}

/*******************************************************************************
 *
 ******************************************************************************/
void Rcs::HUD::resizeNoMutex(int llx_, int lly_, int sizeX_, int sizeY_)
{
  this->llx = llx_;
  this->lly = llx_;
  this->sizeX = sizeX_;
  this->sizeY = sizeY_;
  backgroundVertices->clear();
  backgroundVertices->push_back(osg::Vec3(llx,          lly,         -1.0));
  backgroundVertices->push_back(osg::Vec3(llx + sizeX,  lly,         -1.0));
  backgroundVertices->push_back(osg::Vec3(llx + sizeX,  lly + sizeY, -1.0));
  backgroundVertices->push_back(osg::Vec3(llx,          lly + sizeY, -1.0));
  backgroundChanged = true;
}
