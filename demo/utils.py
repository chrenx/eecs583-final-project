import matplotlib.pyplot as plt
import pickle
import torch
import numpy as np
import pandas as pd


# ============================== Utils =========================================
def process_model_output(model_input, loaded_model):
    """
    input: shape(1, 100, 100)
    output: [#, #, ...]. Len of list is the # of VR. Each # represents a color. 
    """
    print('\n------PREDICTING-------')
    loaded_model.eval()
    with torch.inference_mode():
        predicted = loaded_model(model_input) # torch.Size([1, 100, 101])
        predicted = torch.softmax(predicted, dim=2)
        # print("softmax后predicted shape: ", predicted.shape)
        predicted = predicted.clone().detach().cpu().numpy() # (100, 101)
        # print(predicted.shape) # (1, 100, 101)
        # print(np.argmax(predicted, axis=2)) # (1,100)

        x_pred = model_input.clone().detach().cpu() # torch.Size([1, 100, 100])
        print('\nInvalid edges percentage before color correction -----------------------')
        post_process(np.asarray(x_pred.tolist()), predicted)
        
        print('\nColors list and Chromatic number predicted by the model ->')
        colors_list_list_before_correction = post_process_chromatic(np.asarray(x_pred), predicted)
        print('\nColor Correction -------')
        predicted = post_process_correction(np.asarray(x_pred), predicted, colors_list_list_before_correction)
        # print('\nColors list and Chromatic number following color correction ->')
        colors_list_list_after_correction = post_process_chromatic(np.asarray(x_pred), predicted)
        print('\nInvalid edges percentage after color correction ->')
        post_process(np.asarray(x_pred), predicted)
        return colors_list_list_after_correction

def process_model_input(ig_file, device, seq_size=100):
    """
    input: #, 200 long, 100 #
    output: shape(1, 100, 100)
    """
    print(ig_file, "information:")
    seq = pd.read_csv(ig_file, header=None, low_memory=False)
    columns = seq.columns.tolist()
    #get the adj edges
    adj_edge = np.array(seq[columns[1:2*seq_size+1]])
    #get the colors
    data_color = np.array(seq[columns[2*seq_size+1:3*seq_size+1]])
    X = np.zeros((adj_edge.shape[0], data_color.shape[1], seq_size))
    X, Y = adBits(adj_edge, X, data_color)
    Y = updateLabelBits(X, Y)
    X = torch.tensor(X, dtype=torch.float32).to(device)

    return X

def adBits(x, x2, y, seqsize=100):
   for i in range(x.shape[0]):
      loop = seqsize
      j = 0
      for loop in range(seqsize):     
          adBits_1 = int(x[i][2*loop])

          adBits_2 = int(x[i][2*loop+1])

          if (adBits_1):
              #print adBits
              for k in range(0,64):
                  bit = adBits_1 & 1
                  #print ( ' adBits&1 = ',  bit )
                  # Uncomment below if you want new nodes to only have adjacency to earlier nodes
                  if ( bit == 1):
                  #if ( bit == 1 and k<j):
                      x2[i][j][k] = 1
                      #print x2[i-1][j-1][k-1]
                  else:
                      x2[i][j][k] = 0
                      #print x2[i-1][j-1][k-1]
                  adBits_1 >>= 1
              x2[i][j][j] = 1 

          if (adBits_2):   
              #print adBits
              for k in range(64,100):
                  bit = adBits_2 & 1
                  #print ( ' adBits&1 = ',  bit )
                  if ( bit == 1):
                  #if ( bit == 1 and k<j):
                      x2[i][j][k] = 1
                      #print x2[i-1][j-1][k-1]
                  else:
                      x2[i][j][k] = 0
                      #print x2[i-1][j-1][k-1]
                  adBits_2 >>= 1
              x2[i][j][j] = 1
          j = j+1
   return x2, y

def updateLabelBits(x, y, seqsize=100):
    for i in range(x.shape[0]):
        #print(' for each train sample  ... ', i)
        label_dict = {}
        label_num = 1
        for j in range(seqsize):
            if (x[i][j][j] == 0):
                y[i][j] = 0
            else:
                if y[i][j] in label_dict:
                    y[i][j] = label_dict[y[i][j]]
                else:
                    label_dict[y[i][j]] = label_num
                    y[i][j] = label_num
                    label_num = label_num + 1
    return y

# ============================== Post Process ==================================

def post_process (x2_pred, predicted, seqsize=100):
    #Calculate the number of edges which will require correction
    invCols = 0
    edges = 0
    for i in range(x2_pred.shape[0]):
        for j in range(seqsize): # row
            for k in range(j): # col
                adj = x2_pred[i][j][k]
                if (adj == 1):
                    edges += 1
                    if (np.argmax(predicted[i][j]) == np.argmax(predicted[i][k])):
                        invCols += 1
                        
    print('Total No of edges ', edges)
    print('# of edges with invalid coloring ', invCols)
    print(f'Percentage of invalid colors {(invCols/edges*100):.2f} %')

def post_process_chromatic (x2_pred, predicted, seqsize=100):  
    colors_list_list = []
    for i in range(x2_pred.shape[0]):
        colors_list = []
        for j in range(seqsize):
            # Valid nodes will have below set to 1 so check the color 
            # assignment of those nodes only
            if (x2_pred[i][j][j] != 0):
                colors_list.append(np.argmax(predicted[i][j]))
        # print('Colors list of graph ', i, ' is  \n', colors_list)
        chromatic_number = len(set(colors_list))
        # print('Chromatic number of graph ', i, ' is  ', chromatic_number)
        colors_list_list.append(colors_list)
    return colors_list_list

def post_process_correction (x2_pred, predicted, colors_list_list, seqsize=100): 
  totInvCols = 0
  totEdges = 0

  for i in range(x2_pred.shape[0]):
      #maxcol = max(xpredicted[i])
      maxcol = max(colors_list_list[i])
      #print(maxcol)
      #mcol = maxcol[0]
      maxorigcol = maxcol
      mcolnew = maxcol
      #print('Maxcol = ',maxcol[0])
      #print(' ... FOR SAMPLE  ... ', i)
      invCols = 0
      edges = 0;
      newCol = 500

      for j in range(seqsize):
          #print(' ... ... FOR EACH NODE ... ...', j)
          for k in range(j):
              #print(' ... ... ... for each adjacency  ... ... ...', k)
              adj = x2_pred[i][j][k]
              #There is an edge
              if ( adj == 1 ):
                  edges += 1
                  if ( np.argmax(predicted[i][j]) == np.argmax(predicted[i][k]) ):                   
                      col_j = np.argmax(predicted[i][j])
                      col_k = np.argmax(predicted[i][k])
                      invCols += 1

                      #Check whether we can give one of the existing colors
                      foundfinalcol = 0
                      for  y in range(1,maxcol+1):
                          #print('Check for COLOR NO ... ', y)
                          if ( foundfinalcol == 1 ) :
                              #print('FOUND COLOR ALREADY  ... leave the loop')
                              break

                          foundcol = 0
                          #Check the adjacent nodes of j
                          #for  z in range(j):
                          for z in range(seqsize):
                              if j!=z:
                                  if  (   ((x2_pred[i][j][z] == 1) and (np.argmax(predicted[i][z]) == y))
                                      or  ((x2_pred[i][z][j] == 1) and (np.argmax(predicted[i][z]) == y))
                                      ):
                                      #print('[1] Adjacent node ... from ',j, '-->', z, 'color = ',xpredicted[i][z][0] )
                                      foundcol = 1
                                      #print('[1] Found Color ', y, ' for node ', z, 'from node ', j )
                                      break

                          #Finished checking the adjacent nodes of j
                          #Color y is not used by any of j's neighbours
                          #print('[1] Finished Checking the adjacent node of ... ',j,' ... foundcol = ',foundcol)
                          if ( foundcol == 0 ) :
                              #assign any prediction > 1
                              predicted[i][j][y] = 2
                              #print('[1] Reuse color ', y, ' for node ', j)
                              foundfinalcol = 1

                          else :
                              foundcol = 0                                                            
                              #Check the adjacent nodes of k
                              for z in range(seqsize):
                                  if k!=z:
                                      if  (   ((x2_pred[i][k][z] == 1) and (np.argmax(predicted[i][z]) == y))
                                          or  ((x2_pred[i][z][k] == 1) and (np.argmax(predicted[i][z]) == y))
                                          ):
                                          #print('[1] Adjacent node ... from ',j, '-->', z, 'color = ',xpredicted[i][z][0] )
                                          foundcol = 1
                                          #print('[1] Found Color ', y, ' for node ', z, 'from node ', j )
                                          break
                              #Color y is not used by any of k's neighbours
                              if ( foundcol == 0 ) :
                                  #assign any prediction > 1
                                  predicted[i][k][y] = 2
                                  #print('[2] Reuse color ', y, ' for node ', k )
                                  foundfinalcol = 1

                      # Could not color using an existing color
                      # Get a new color from 500 onwards OR use from the new 500 color number series
                      if ( foundfinalcol == 0 ) :
                           #newCol += 1
                           mcolnew += 1
                           #assign any prediction > 1
                           predicted[i][k][mcolnew] = 2
                           maxcol +=1
                           #print('Use new color ', mcolnew, ' for node ', k)

  return predicted

