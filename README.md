train server template<br>
所有火車: 5台順向，5台逆向，共10台<br>
每台火車: 10個座位，站點到站點間有獨立的佔位情形<br>
每台火車: 一個schedule，每個站點儲存一個時間，這個時間代表出發時間( 我們假設火車到達即出發，無縫銜接)<br>
所有站點: "Taipei", "Taoyuan", "Taichung", "Tainan", "Kaohsiung"<br>
查詢班次格式: check_schedule start destination time ==>  check_schedule Taipei Taoyuan 2024/12/21/8:00<br>
選擇班次格式: book_ticket  trainID start destination amount ID ==> book_ticket 8 Kaohsiung Taipei 3 A130694280<br>


Client 查詢班次，server回傳直達與轉車方案:<br>
![image](https://github.com/user-attachments/assets/67ebc40e-8426-4420-8bac-8b7769529147)<br><br>

Client 下定車票，選擇Train3，並且訂了7張票<br>
![image](https://github.com/user-attachments/assets/6c0744a9-80a3-4834-8e7f-0b83f3920107)<br><br>

再次查看班次train3的座位少了7張<br>
![image](https://github.com/user-attachments/assets/f81aa170-0ab9-4f31-81a1-ab3f0dd9260a)<br><br>

