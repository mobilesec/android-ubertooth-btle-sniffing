/**
 * Copyright (C) 2009, 2010 SC 4ViewSoft SRL
 *  
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *  
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.gnychis.ubertooth.Interfaces;

import java.util.ArrayList;
import java.util.List;

import org.achartengine.ChartFactory;
import org.achartengine.chart.PointStyle;
import org.achartengine.renderer.XYMultipleSeriesRenderer;
import org.achartengine.renderer.XYSeriesRenderer;

import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Paint.Align;

import com.gnychis.ubertooth.UbertoothMain;
import com.gnychis.ubertooth.DeviceHandlers.UbertoothOne;

/**
 * Class extending AbstractDemoChart for displaying graph spectrum
 * @author Philipp
 *
 */
public class GraphSpectrum extends AbstractDemoChart {
	
	int maxresults[];
	UbertoothMain _mainActivity;

	public String getName() {return "Signal Across Spectrum";}
	public String getDesc() {return "The average temperature in 4 Greek islands (line chart)";}
	
	public GraphSpectrum (UbertoothMain main) {
		_mainActivity=main;
	}


  public Intent execute(Context context) {
	  	  
	  String[] titles = new String[] { "2.4GHz" };
	  int nFreqBins = UbertoothOne.BT_HIGH_FREQ-UbertoothOne.BT_LOW_FREQ;
    
	  // First, populate the data for the x-axis
	  List<double[]> x = new ArrayList<double[]>();
	  double xd[] = new double[nFreqBins];
	  for(int i=0; i<nFreqBins; i++)
		  xd[i]=UbertoothOne.BT_LOW_FREQ+i;
	  x.add(xd);
    
	  // Then, populate the data for the y-axis
	  List<double[]> y = new ArrayList<double[]>();
	  double yd[] = new double[nFreqBins];
	  for(int i=0; i<nFreqBins; i++)
		  yd[i] = _mainActivity._scan_result.get(i);
	  y.add(yd);
	  
	  // Setup some related things for the asthetics of the chart
	  int[] colors = new int[] { Color.BLUE};
	  PointStyle[] styles = new PointStyle[] { PointStyle.POINT, PointStyle.POINT, PointStyle.POINT, PointStyle.POINT };
	  XYMultipleSeriesRenderer renderer = buildRenderer(colors, styles);
	  int length = renderer.getSeriesRendererCount();
	  for (int i = 0; i < length; i++)
		  ((XYSeriesRenderer) renderer.getSeriesRendererAt(i)).setFillPoints(true);
    
	  setChartSettings(renderer, "2.4GHz Spectrum", "Frequency", "Signal Strength (dBm)", UbertoothOne.BT_LOW_FREQ, UbertoothOne.BT_HIGH_FREQ, -100, 0, Color.LTGRAY, Color.LTGRAY);
    
	  renderer.setXLabels(12);
	  renderer.setYLabels(10);
	  renderer.setShowGrid(true);
	  renderer.setXLabelsAlign(Align.RIGHT);
	  renderer.setYLabelsAlign(Align.RIGHT);
	  renderer.setPanLimits(new double[] { -50, 300, -150, 0 });
	  renderer.setZoomLimits(new double[] { -50, 300, -150, 0 });
	  Intent intent = ChartFactory.getLineChartIntent(context, buildDataset(titles, x, y), renderer, "2.4GHz Spectrum");
	  return intent;
  }
}
