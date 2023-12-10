from torch import nn

class DLRegAlloc(nn.Module):
    def __init__(self):
        super().__init__()
        self.lstm_1 = nn.LSTM(input_size=100, hidden_size=512, 
                              batch_first=True, bidirectional=True)
        self.lstm_2 = nn.LSTM(input_size=1024, hidden_size=256, 
                              batch_first=True,bidirectional=True)
        self.lstm_3 = nn.LSTM(input_size=512, hidden_size=128, 
                              batch_first=True,bidirectional=True)
        self.dropout = nn.Dropout(0.05)
        self.fc = nn.Linear(in_features=256, out_features=101)
        # self.softmax = nn.Softmax(dim=2)
        # self.time_distributed = TimeDistributed(self.fc, batch_first=True)
        
    def forward(self, x):
        # print("----: ", x.shape)
        x, _ = self.lstm_1(x)
        # print("lstm1 shape: ", x.shape)
        x = self.dropout(x)

        x, _ = self.lstm_2(x)
        # print("lstm2 shape: ", x.shape)
        x = self.dropout(x)

        x, _ = self.lstm_3(x)
        # print("lstm3 shape: ", x.shape)
        x = self.dropout(x)

        x = self.fc(x)
        # print("fc shape: ", x.shape)
        # x = self.softmax(x)
        # x = self.time_distributed(x)
        # print("time_distributed shape: ", x.shape)

        return x