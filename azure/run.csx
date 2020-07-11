#r "Newtonsoft.Json"

using System;
using System.Text;
using System.Net;
using System.Net.Http;
using Microsoft.AspNetCore.Mvc;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

private static readonly string[] BEER_GLASSES = {
    "https://openclipart.org/image/400px/svg_to_png/310951/1543338098.png", // stoutglas
    "https://openclipart.org/image/400px/svg_to_png/306498/1536745172.png", // helles glas
    "https://openclipart.org/image/400px/svg_to_png/303105/1528701175.png", // dunkler humpen
    "https://openclipart.org/image/400px/svg_to_png/14854/nicubunu-Beer-mug.png", // heller humpen
    "https://openclipart.org/image/400px/svg_to_png/189845/1388707137.png" // mathematischer humpen
};

private static readonly string[] BEER_KEGS = {
    "https://openclipart.org/image/400px/svg_to_png/316628/1552405761.png", // mit zapf
    "https://openclipart.org/image/400px/svg_to_png/279569/BarrelInSling.png" // kran
};

private const string POSTMAN_URL = "https://postman-echo.com/post"; // use for testing (echo)
private const string TEAMS_URL = "https://outlook.office.com/webhook/533660c9-9b0e-4f08-b3e2-5666e0f84294@cd7b491b-fd4a-4dd1-8b0c-f720f0afdc96/IncomingWebhook/a3859e193e104e92bf1867458c7c3d87/34151926-7a57-4a5e-81f0-82e149e58593";

// Buzzer can handle following strings
private const string BUZZER_OK = "OK";
private const string BUZZER_EARLY = "EARLY";
private const string BUZZER_REFUSE = "REFUSE";
private const string BUZZER_ERROR = "ERROR";

private static HttpClient httpClient = new HttpClient();

public static async Task<IActionResult> Run(HttpRequest req, ILogger log)
{
    log.LogInformation("Trigger function processed a request.");

    bool isTestMode = true;
    switch (req.Query["action"])
    {
        case "push":
            isTestMode = false;
            break;
    }

    DateTime swissTime = GetSwissTime();
    if (swissTime.Hour >= 16 && swissTime.Hour < 21)
    {
        // gueti zit zum trinke:
        string jsonContent = getJSONCard("Bier", "Chunnsch o?", "Biud");
        pushMessage(jsonContent, log, isTestMode);

        return new OkObjectResult(BUZZER_OK);
    }
    if (swissTime.Hour >= 8 && swissTime.Hour < 16)
    {
        // no chli früeh:
        return new OkObjectResult(BUZZER_EARLY);
    }
    else
    {
        // schlächti zit:
        return new OkObjectResult(BUZZER_REFUSE);
    }
}

private static DateTime GetSwissTime()
{
    DateTime timeUtc = DateTime.UtcNow;
    TimeZoneInfo swissTimeZone = TimeZoneInfo.FindSystemTimeZoneById("W. Europe Standard Time");
    DateTime swissTime = TimeZoneInfo.ConvertTimeFromUtc(timeUtc, swissTimeZone);

    return swissTime;
}

private static bool pushMessage(string jsonContent, ILogger log, bool isTestMode)
{
    string url = isTestMode ? POSTMAN_URL : POSTMAN_URL;

    log.LogInformation("Pushing message to: " + url);
    HttpContent httpContent = new StringContent(jsonContent, Encoding.UTF8, "application/json");
    HttpResponseMessage response = httpClient.PostAsync(url, httpContent).Result;

    string result = response.Content.ReadAsStringAsync().Result;
    log.LogInformation(result);

    return response.IsSuccessStatusCode;
}

public static string getJSONCard(string title, string subtitle, string image)
{
    string json =
@"{
    ""@type"": ""MessageCard"",
    ""@context"": ""http://schema.org/extensions"",
    ""summary"": ""Nachricht vom Bierbuzzer"",
    ""sections"": [
      {
        ""activityTitle"": ""Es isch zit für Bier"",
        ""activitySubtitle"": ""Bisch ou derbi?"",
        ""activityImage"": """",
        ""facts"": [
          {
            ""name"": ""Kegerator"",
            ""value"": ""vermuetlech parat""
          },
          {
            ""name"": ""Temperatur"",
            ""value"": ""gmüetlech chüeu""
          },
          {
            ""name"": ""Füustand"",
            ""value"": ""es het no""
          }
        ],
        ""markdown"": true
      }
    ]
  }";

    dynamic card = JObject.Parse(json);
    card.sections[0].activityTitle = title;
    card.sections[0].activityTitle = subtitle;
    card.sections[0].activityImage = image;
    return card.ToString();
}
