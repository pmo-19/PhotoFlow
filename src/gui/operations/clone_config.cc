/* 
 */

/*

    Copyright (C) 2014 Ferrero Andrea

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program. If not, see <http://www.gnu.org/licenses/>.


 */

/*

    These files are distributed with PhotoFlow - http://aferrero2707.github.io/PhotoFlow/

 */

#include "../../operations/brightness_contrast.hh"

#include "clone_config.hh"


PF::CloneConfigDialog::CloneConfigDialog( PF::Layer* layer ):
  OperationConfigDialog( layer, _("Clone layer") ),
  layer_list( this, _("Layer name:") ),
  sourceSelector( this, "source_channel", _("Source channel: "), 1 )
{
  add_widget( layer_list );
  add_widget( sourceSelector );

  //fileEntry.signal_activate().
  //  connect(sigc::mem_fun(*this,
  //			  &CloneConfigDialog::on_filename_changed));
}


void PF::CloneConfigDialog::on_layer_changed()
{
  if( get_layer() && get_layer()->get_image() && 
      get_layer()->get_processor() &&
      get_layer()->get_processor()->get_par() ) {
  }
}


void PF::CloneConfigDialog::do_update()
{
  layer_list.update_model();
  OperationConfigDialog::do_update();
}


void PF::CloneConfigDialog::init()
{
  layer_list.update_model();
  OperationConfigDialog::init();
}
